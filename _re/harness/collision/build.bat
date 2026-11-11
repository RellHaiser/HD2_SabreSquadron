@echo off
rem The collision world (game/collision*.cpp), MinGW; runs collision_test.exe ("collision: N checks, 0 failures").
rem Input: the levels' tree.klz files (MISSIONS_DIR, default H:\HD2_Unpacked\MISSIONS). Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\collision_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% collision_test.cpp globals.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp "%TEMP%\collision_math.o" -o "%TEMP%\collision_test.exe" 2>&1
"%TEMP%\collision_test.exe"
