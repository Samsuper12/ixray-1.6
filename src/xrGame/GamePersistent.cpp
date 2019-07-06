#include "stdafx.h"
#include "gamepersistent.h"
#include "../xrEngine/fmesh.h"
#include "../xrEngine/xr_ioc_cmd.h"

#include "../xrEngine/gamemtllib.h"
#include "../xrEngine/Environment.h"
#include "../Include/xrRender/Kinematics.h"
#include "profiler.h"
#include "lobby_menu.h"
#include "UICursor.h"
#include "game_base_space.h"
#include "level.h"
#include "ParticlesObject.h"

#include "actor.h"
#include "spectator.h"
#include "UI/UItextureMaster.h"
#include "UI/UIGameTutorial.h"
#include "UIGameCustom.h"
#include "ui/UIMainIngameWnd.h"
#include "string_table.h"
#include "../xrEngine/x_ray.h"
#include "hudmanager.h"

CGamePersistent::CGamePersistent(void)
{
	m_bPickableDOF				= false;
	ambient_effect_next_time	= 0;
	ambient_effect_stop_time	= 0;
	ambient_particles			= 0;

	ambient_effect_wind_start	= 0.f;
	ambient_effect_wind_in_time	= 0.f;
	ambient_effect_wind_end		= 0.f;
	ambient_effect_wind_out_time= 0.f;
	ambient_effect_wind_on		= false;

	ZeroMemory					(ambient_sound_next_time, sizeof(ambient_sound_next_time));
	

	m_pUI_core					= NULL;
	m_pMainMenu					= NULL;
	m_intro						= NULL;
//.	m_intro_event.bind			(this, &CGamePersistent::start_logo_intro);
#ifdef DEBUG
	m_frame_counter				= 0;
	m_last_stats_frame			= u32(-2);
#endif

	Fvector3* DofValue		= pConsoleCommands->GetFVectorPtr("r2_dof");
	SetBaseDof				(*DofValue);
}

CGamePersistent::~CGamePersistent(void)
{	
	Device.seqFrame.Remove		(this);
}

void CGamePersistent::RegisterModel(IRenderVisual* V)
{
	// Check types
	switch (V->getType()){
	case MT_SKELETON_ANIM:
	case MT_SKELETON_RIGID:{
		u16 def_idx		= GMLib.GetMaterialIdx("default_object");
		R_ASSERT2		(GMLib.GetMaterialByIdx(def_idx)->Flags.is(SGameMtl::flDynamic),"'default_object' - must be dynamic");
		IKinematics* K	= smart_cast<IKinematics*>(V); VERIFY(K);
		int cnt = K->LL_BoneCount();
		for (u16 k=0; k<cnt; k++){
			CBoneData& bd	= K->LL_GetData(k); 
			if (*(bd.game_mtl_name)){
				bd.game_mtl_idx	= GMLib.GetMaterialIdx(*bd.game_mtl_name);
				R_ASSERT2(GMLib.GetMaterialByIdx(bd.game_mtl_idx)->Flags.is(SGameMtl::flDynamic),"Required dynamic game material");
			}else{
				bd.game_mtl_idx	= def_idx;
			}
		}
	}break;
	}
}

extern void clean_game_globals	();
extern void init_game_globals	();

void CGamePersistent::OnAppStart()
{
	// load game materials
	GMLib.Load					();
	init_game_globals			();
	__super::OnAppStart			();
	m_pUI_core					= xr_new<ui_core>();
	if(!g_dedicated_server)
		m_pMainMenu					= xr_new<lobby_menu>();
}


void CGamePersistent::OnAppEnd	()
{
	if(m_pMainMenu && m_pMainMenu->IsActive())
		m_pMainMenu->Activate(false);

	xr_delete					(m_pMainMenu);
	xr_delete					(m_pUI_core);

	__super::OnAppEnd			();

	clean_game_globals			();

	GMLib.Unload				();

}

void CGamePersistent::Disconnect()
{
	// destroy ambient particles
	CParticlesObject::Destroy(ambient_particles);

	__super::Disconnect			();
	// stop all played emitters
	::Sound->stop_emitters		();
}


void CGamePersistent::OnGameStart()
{
	__super::OnGameStart		();
	g_current_keygroup = _mp;
}

void CGamePersistent::WeathersUpdate()
{
	if (g_pGameLevel && !g_dedicated_server)
	{
		CActor* actor				= smart_cast<CActor*>(Level().CurrentViewActor());
		BOOL bIndoor				= TRUE;
		if (actor) bIndoor			= actor->renderable_ROS()->get_luminocity_hemi()<0.05f;

		int data_set				= (Random.randF()<(1.f-Environment().CurrentEnv->weight))?0:1; 
		
		CEnvDescriptor* const current_env	= Environment().Current[0]; 
		VERIFY						(current_env);

		CEnvDescriptor* const _env	= Environment().Current[data_set]; 
		VERIFY						(_env);

		CEnvAmbient* env_amb		= _env->env_ambient;
		if (env_amb) {
			CEnvAmbient::SSndChannelVec& vec	= current_env->env_ambient->get_snd_channels();
			CEnvAmbient::SSndChannelVecIt I		= vec.begin();
			CEnvAmbient::SSndChannelVecIt E		= vec.end();
			
			for (u32 idx=0; I!=E; ++I,++idx) {
				CEnvAmbient::SSndChannel& ch	= **I;
				R_ASSERT						(idx<20);
				if(ambient_sound_next_time[idx]==0)//first
				{
					ambient_sound_next_time[idx] = Device.dwTimeGlobal + ch.get_rnd_sound_first_time();
				}else
				if(Device.dwTimeGlobal > ambient_sound_next_time[idx])
				{
					ref_sound& snd					= ch.get_rnd_sound();

					Fvector	pos;
					float	angle		= ::Random.randF(PI_MUL_2);
					pos.x				= _cos(angle);
					pos.y				= 0;
					pos.z				= _sin(angle);
					pos.normalize		().mul(ch.get_rnd_sound_dist()).add(Device.vCameraPosition);
					pos.y				+= 10.f;
					snd.play_at_pos		(0,pos);

#ifdef DEBUG
					if (!snd._handle() && strstr(Core.Params,"-nosound"))
						continue;
#endif // DEBUG

					VERIFY							(snd._handle());
					u32 _length_ms					= iFloor(snd.get_length_sec()*1000.0f);
					ambient_sound_next_time[idx]	= Device.dwTimeGlobal + _length_ms + ch.get_rnd_sound_time();
				}
			}
			// start effect
			if ((FALSE==bIndoor) && (0==ambient_particles) && Device.dwTimeGlobal>ambient_effect_next_time){
				CEnvAmbient::SEffect* eff			= env_amb->get_rnd_effect(); 
				if (eff){
					Environment().wind_gust_factor	= eff->wind_gust_factor;
					ambient_effect_next_time		= Device.dwTimeGlobal + env_amb->get_rnd_effect_time();
					ambient_effect_stop_time		= Device.dwTimeGlobal + eff->life_time;
					ambient_effect_wind_start		= Device.fTimeGlobal;
					ambient_effect_wind_in_time		= Device.fTimeGlobal + eff->wind_blast_in_time;
					ambient_effect_wind_end			= Device.fTimeGlobal + eff->life_time/1000.f;
					ambient_effect_wind_out_time	= Device.fTimeGlobal + eff->life_time/1000.f + eff->wind_blast_out_time;
					ambient_effect_wind_on			= true;
										
					ambient_particles				= CParticlesObject::Create(eff->particles.c_str(),FALSE,false);
					Fvector pos; pos.add			(Device.vCameraPosition,eff->offset); 
					ambient_particles->play_at_pos	(pos);
					if (eff->sound._handle())		eff->sound.play_at_pos(0,pos);


					Environment().wind_blast_strength_start_value=Environment().wind_strength_factor;
					Environment().wind_blast_strength_stop_value=eff->wind_blast_strength;

					if (Environment().wind_blast_strength_start_value==0.f)
					{
						Environment().wind_blast_start_time.set(0.f,eff->wind_blast_direction.x,eff->wind_blast_direction.y,eff->wind_blast_direction.z);
					}
					else
					{
						Environment().wind_blast_start_time.set(0.f,Environment().wind_blast_direction.x,Environment().wind_blast_direction.y,Environment().wind_blast_direction.z);
					}
					Environment().wind_blast_stop_time.set(0.f,eff->wind_blast_direction.x,eff->wind_blast_direction.y,eff->wind_blast_direction.z);
				}
			}
		}
		if (Device.fTimeGlobal>=ambient_effect_wind_start && Device.fTimeGlobal<=ambient_effect_wind_in_time && ambient_effect_wind_on)
		{
			float delta=ambient_effect_wind_in_time-ambient_effect_wind_start;
			float t;
			if (delta!=0.f)
			{
				float cur_in=Device.fTimeGlobal-ambient_effect_wind_start;
				t=cur_in/delta;
			}
			else
			{
				t=0.f;
			}
			Environment().wind_blast_current.slerp(Environment().wind_blast_start_time,Environment().wind_blast_stop_time,t);

			Environment().wind_blast_direction.set(Environment().wind_blast_current.x,Environment().wind_blast_current.y,Environment().wind_blast_current.z);
			Environment().wind_strength_factor=Environment().wind_blast_strength_start_value+t*(Environment().wind_blast_strength_stop_value-Environment().wind_blast_strength_start_value);
		}

		// stop if time exceed or indoor
		if (bIndoor || Device.dwTimeGlobal>=ambient_effect_stop_time){
			if (ambient_particles)					ambient_particles->Stop();
			
			Environment().wind_gust_factor		= 0.f;
			
		}

		if (Device.fTimeGlobal>=ambient_effect_wind_end && ambient_effect_wind_on)
		{
			Environment().wind_blast_strength_start_value=Environment().wind_strength_factor;
			Environment().wind_blast_strength_stop_value	=0.f;

			ambient_effect_wind_on=false;
		}

		if (Device.fTimeGlobal>=ambient_effect_wind_end &&  Device.fTimeGlobal<=ambient_effect_wind_out_time)
		{
			float delta=ambient_effect_wind_out_time-ambient_effect_wind_end;
			float t;
			if (delta!=0.f)
			{
				float cur_in=Device.fTimeGlobal-ambient_effect_wind_end;
				t=cur_in/delta;
			}
			else
			{
				t=0.f;
			}
			Environment().wind_strength_factor=Environment().wind_blast_strength_start_value+t*(Environment().wind_blast_strength_stop_value-Environment().wind_blast_strength_start_value);
		}
		if (Device.fTimeGlobal>ambient_effect_wind_out_time && ambient_effect_wind_out_time!=0.f )
		{			
			Environment().wind_strength_factor=0.0;
		}

		// if particles not playing - destroy
		if (ambient_particles&&!ambient_particles->IsPlaying())
			CParticlesObject::Destroy(ambient_particles);
	}
}

//bool allow_game_intro( )
//{
//	return false;
//}
//
//bool allow_logo_intro( )
//{
//	return false;
//}

//void CGamePersistent::start_logo_intro()
//{
//	if(Device.dwPrecacheFrame==0)
//	{
//		m_intro_event.bind		(this, &CGamePersistent::update_logo_intro);
//		if(allow_logo_intro() && !g_dedicated_server && NULL==g_pGameLevel )
//		{
//			VERIFY				(NULL==m_intro);
//			m_intro				= xr_new<CUISequencer>();
//			m_intro->Start		( "intro_logo" );
//			pConsole->Hide		( );
//		}else
//			pConsoleCommands->Execute	("main_menu on" );
//
//	}
//}
//
//void CGamePersistent::update_logo_intro()
//{
//	if(m_intro && (false==m_intro->IsActive()))
//	{
//		m_intro_event			= 0;
//		xr_delete				( m_intro );
//		pConsoleCommands->Execute		("main_menu on" );
//	}else
//	if(!m_intro)
//	{
//		m_intro_event			= 0;
//	}
//}
//

CUISequencer* g_tutorial = NULL;
CUISequencer* g_tutorial2 = NULL;

void CGamePersistent::OnFrame	()
{
	if(g_tutorial2)
	{ 
		g_tutorial2->Destroy	();
		xr_delete				(g_tutorial2);
	}

	if(g_tutorial && !g_tutorial->IsActive())
	{
		xr_delete(g_tutorial);
	}
	if(0==Device.dwFrame%200)
		CUITextureMaster::FreeCachedShaders();

#ifdef DEBUG
	++m_frame_counter;
#endif
	if(!g_dedicated_server)
	{
		if (!m_intro_event.empty())	m_intro_event();

		if(Device.dwPrecacheFrame==0 && !m_intro && m_intro_event.empty())
			load_screen_renderer.stop();

		if( !m_pMainMenu->IsActive() )
			m_pMainMenu->DestroyInternal(false);
	}
	


	if(!g_pGameLevel)			return;
	if(!g_pGameLevel->bReady)	return;

	__super::OnFrame			();

	if(!Device.Paused())
		Engine.Sheduler.Update		();

	// update weathers ambient
	if(!Device.Paused())
		WeathersUpdate				();


#ifdef DEBUG
	if ((m_last_stats_frame + 1) < m_frame_counter)
		profiler().clear		();
#endif
	UpdateDof();
}

void CGamePersistent::OnEvent(EVENT E, u64 P1, u64 P2)
{
	//__super::OnEvent( E, P1, P2);
	//if(E==eDemoStart)
	//{
	//	string256			cmd;
	//	LPCSTR				demo	= LPCSTR(P1);
	//	xr_sprintf				(cmd,"demo_play %s",demo);
	//	pConsoleCommands->Execute	(cmd);
	//	xr_free				(demo);
	//	uTime2Change		= Device.TimerAsync() + u32(P2)*1000;
	//}
}

void CGamePersistent::Statistics	(CGameFont* F)
{
#ifdef DEBUG
#	ifndef _EDITOR
		m_last_stats_frame		= m_frame_counter;
		profiler().show_stats	(F,true);
#	endif
#endif
}

float CGamePersistent::MtlTransparent(u32 mtl_idx)
{
	return GMLib.GetMaterialByIdx((u16)mtl_idx)->fVisTransparencyFactor;
}
static BOOL bRestorePause	= FALSE;
static BOOL bEntryFlag		= TRUE;

void CGamePersistent::OnAppActivate		()
{
	bool bIsMP = (g_pGameLevel && Level().game );
	bIsMP		&= !Device.Paused();

	if( !bIsMP )
	{
		Device.Pause			(FALSE, !bRestorePause, TRUE, "CGP::OnAppActivate");
	}else
	{
		Device.Pause			(FALSE, TRUE, TRUE, "CGP::OnAppActivate MP");
	}

	bEntryFlag = TRUE;
}

void CGamePersistent::OnAppDeactivate	()
{
	if(!bEntryFlag) return;

	bool bIsMP = (g_pGameLevel && Level().game);

	bRestorePause = FALSE;

	if ( !bIsMP )
	{
		bRestorePause			= Device.Paused();
		Device.Pause			(TRUE, TRUE, TRUE, "CGP::OnAppDeactivate");
	}else
	{
		Device.Pause			(TRUE, FALSE, TRUE, "CGP::OnAppDeactivate MP");
	}
	bEntryFlag = FALSE;
}


bool CGamePersistent::OnRenderPPUI_query()
{
	return MainMenu()->OnRenderPPUI_query();
	// enable PP or not
}

extern void draw_wnds_rects();
void CGamePersistent::OnRenderPPUI_main()
{
	// always
	MainMenu()->OnRenderPPUI_main();
	draw_wnds_rects();
}

void CGamePersistent::OnRenderPPUI_PP()
{
	MainMenu()->OnRenderPPUI_PP();
}

void CGamePersistent::LoadTitle(bool change_tip, shared_str map_name)
{
	pApp->LoadStage();
	if(change_tip)
	{
		string512				buff;
		u8						tip_num = 0;
		xr_sprintf				(buff, "%s%d:", CStringTable().translate("ls_tip_number").c_str(), tip_num);
		shared_str				tmp = buff;
		xr_sprintf				(buff, "ls_mp_tip_%d", tip_num);
		pApp->LoadTitleInt		(CStringTable().translate("ls_header").c_str(), tmp.c_str(), CStringTable().translate(buff).c_str());
	}
}

bool CGamePersistent::CanBePaused()
{
	return (g_pGameLevel!=NULL);
}

void CGamePersistent::SetPickableEffectorDOF(bool bSet)
{
	m_bPickableDOF = bSet;
	if(!bSet)
		RestoreEffectorDOF();
}

void CGamePersistent::GetCurrentDof(Fvector3& dof)
{
	dof = m_dof[1];
}

void CGamePersistent::SetBaseDof(const Fvector3& dof)
{
	m_dof[0]=m_dof[1]=m_dof[2]=m_dof[3]	= dof;
}

void CGamePersistent::SetEffectorDOF(const Fvector& needed_dof)
{
	if(m_bPickableDOF)	return;
	m_dof[0]	= needed_dof;
	m_dof[2]	= m_dof[1]; //current
}

void CGamePersistent::RestoreEffectorDOF()
{
	SetEffectorDOF			(m_dof[3]);
}

//	m_dof		[4];	// 0-dest 1-current 2-from 3-original
void CGamePersistent::UpdateDof()
{
	static float diff_far	= pSettings->r_float("zone_pick_dof","far");//70.0f;
	static float diff_near	= pSettings->r_float("zone_pick_dof","near");//-70.0f;

	if(m_bPickableDOF)
	{
		Fvector pick_dof;
		pick_dof.y	= HUD().GetCurrentRayQuery().range;
		pick_dof.x	= pick_dof.y+diff_near;
		pick_dof.z	= pick_dof.y+diff_far;
		m_dof[0]	= pick_dof;
		m_dof[2]	= m_dof[1]; //current
	}
	if(m_dof[1].similar(m_dof[0]))
						return;

	float td			= Device.fTimeDelta;
	Fvector				diff;
	diff.sub			(m_dof[0], m_dof[2]);
	diff.mul			(td/0.2f); //0.2 sec
	m_dof[1].add		(diff);
	(m_dof[0].x<m_dof[2].x)?clamp(m_dof[1].x,m_dof[0].x,m_dof[2].x):clamp(m_dof[1].x,m_dof[2].x,m_dof[0].x);
	(m_dof[0].y<m_dof[2].y)?clamp(m_dof[1].y,m_dof[0].y,m_dof[2].y):clamp(m_dof[1].y,m_dof[2].y,m_dof[0].y);
	(m_dof[0].z<m_dof[2].z)?clamp(m_dof[1].z,m_dof[0].z,m_dof[2].z):clamp(m_dof[1].z,m_dof[2].z,m_dof[0].z);
}

void CGamePersistent::OnSectorChanged(int sector)
{
	if(CurrentGameUI())
		CurrentGameUI()->UIMainIngameWnd->OnSectorChanged(sector);
}

void CGamePersistent::OnAssetsChanged()
{
	IGame_Persistent::OnAssetsChanged	();
	CStringTable().rescan				();
}
