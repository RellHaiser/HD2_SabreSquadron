@echo off
rem The sphere sweeps against one shape against the original executable's (sweep_diff.cpp), MinGW; runs sweep_diff.exe
rem ("sweep diff: N checks, 0 failures"). Needs the unpacked executable (HD2_ORIGINAL_EXE) and the game's DLLs
rem (HD2_GAME_DIR); linked away from 0x400000, where the executable is put. Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\sweep_diff_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I. sweep_diff.cpp original_exe.cpp globals.cpp -Wl,--image-base=0x20000000 %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp "%TEMP%\sweep_diff_math.o" -o "%TEMP%\sweep_diff.exe" 2>&1
"%TEMP%\sweep_diff.exe"
