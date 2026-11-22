@echo off
rem Human part 82: the meleeTracker actor (game/melee_tracker.cpp) against the original executable's (mt_diff.cpp),
rem MinGW; melee_tracker_ai.cpp linked (SetController's variants), the chunks real (common/chunk.cpp); runs mt_diff.exe
rem ("mt_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -I..\interface mt_diff.cpp mt_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 -Wl,--wrap=_ZN24C_MeleeTrackerController7DisableEv %SRC%\game\melee_tracker_ai.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp -lwinmm -o "%TEMP%\mt_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\mt_diff.exe" mt_diff.exe >nul
"%TEMP%\mt_diff.exe"
