#pragma once
#include "object_interfaces.h"
#include "map_location_defs.h"

class CMapLocationWrapper;
class CInventoryOwner;
class CMapLocation;

class CMapManager
{
	Locations				m_locations;
	xr_vector<CMapLocation*> m_deffered_destroy_queue;
public:

							CMapManager					();
							~CMapManager				();
	void	__stdcall		Update						();
	/*ICF */Locations&		Locations					();//{return *m_locations;}
	CMapLocation*			AddMapLocation				(const shared_str& spot_type, u16 id);
	void					RemoveMapLocation			(const shared_str& spot_type, u16 id);
	bool					HasMapLocation				(const shared_str& spot_type, u16 id);
	void					RemoveMapLocationByObjectID (u16 id); //call on destroy object
	void					RemoveMapLocation			(CMapLocation* ml);
	CMapLocation*			GetMapLocation				(const shared_str& spot_type, u16 id);
	void					GetMapLocations				(const shared_str& spot_type, u16 id, xr_vector<CMapLocation*>& res);
	void					DisableAllPointers			();
	bool					GetMapLocationsForObject	(u16 id, xr_vector<CMapLocation*>& res);
	void					OnObjectDestroyNotify		(u16 id);
#ifdef DEBUG
	void					Dump						();
#endif
	void					Destroy						(CMapLocation*);
};
