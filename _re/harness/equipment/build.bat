@echo off
rem The equipment (game/human_equipment.cpp; part 1: the holsters, the models' places, save / load), MinGW; runs
rem equipment_test.exe ("equipment: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
rem The LS3DF port's math is built for x87 (main sets PC_24, as the game runs it): the quaternions loaded are
rem normalised again, and only the game's arithmetic gives the original's bits.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\equipment_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% equipment_test.cpp equipment_globals.cpp %SRC%\game\human_equipment.cpp %SRC%\game\equipment_frames.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\equipment_math.o" -o "%TEMP%\equipment_test.exe" 2>&1
"%TEMP%\equipment_test.exe"
