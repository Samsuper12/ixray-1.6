@echo off

cd libraries

if exist editor.lib (
    del editor.lib
    del editor.exp
)

coff2omf.exe BugTrap.lib BugTrapB.lib
coff2omf.exe ETools.lib EToolsB.lib
coff2omf.exe LWO.lib LWOB.lib
coff2omf.exe DXT.lib DXTB.lib
coff2omf.exe xrPhysics.lib xrPhysicsB.lib
