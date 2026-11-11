@echo off
rem The item system (game/items_module.cpp, item_instances.cpp, item_defs.cpp, item_containers.cpp), MinGW; runs
rem items_test.exe ("items: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
rem Inputs: ITEMS_SAV (default G:\Hidden & Dangerous 2\Tables\items.sav, the table the original game read when the
rem save was made) and EQUIPMENT_SAVE (default %TEMP%\eq_saves\original_SMCAM_quick.sav, a quick save of the
rem original; not in the repository).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\items_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% items_test.cpp items_globals.cpp %SRC%\game\items_module.cpp %SRC%\game\item_instances.cpp %SRC%\game\item_defs.cpp %SRC%\game\item_containers.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\items_math.o" -o "%TEMP%\items_test.exe" 2>&1
"%TEMP%\items_test.exe"
