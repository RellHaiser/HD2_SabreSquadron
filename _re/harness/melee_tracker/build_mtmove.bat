@echo off
rem Human part 81: the meleeTracker's movement (game/melee_tracker_movement.cpp) against the original executable's
rem (mtmove_diff.cpp), MinGW; the mixer linked (part 80's), the chunks real on both sides (common/chunk.cpp: the data
rem files in memory); runs mtmove_diff.exe ("mtmove_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -I..\interface mtmove_diff.cpp mtmove_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\game\melee_tracker_anim_mixer.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp -o "%TEMP%\mtmove_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\mtmove_diff.exe" mtmove_diff.exe >nul
"%TEMP%\mtmove_diff.exe"
