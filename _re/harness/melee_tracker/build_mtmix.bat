@echo off
rem Human part 80: the meleeTracker's animation mixer (game/melee_tracker_anim_mixer.cpp) against the original
rem executable's (mtmix_diff.cpp), MinGW; the chunks real on both sides (common/chunk.cpp: the data files in memory);
rem runs mtmix_diff.exe ("mtmix_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -I..\interface mtmix_diff.cpp mtmix_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp -o "%TEMP%\mtmix_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\mtmix_diff.exe" mtmix_diff.exe >nul
"%TEMP%\mtmix_diff.exe"
