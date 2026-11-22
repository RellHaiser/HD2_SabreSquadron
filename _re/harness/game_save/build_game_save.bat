@echo off
rem Human part 79: the game's saves, loads and restarts (game/game_save.cpp) against the original executable's
rem (game_save_diff.cpp), MinGW; the chunks real on both sides (common/chunk.cpp: the data files in memory); runs
rem game_save_diff.exe ("game_save_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -I..\interface game_save_diff.cpp game_save_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp -o "%TEMP%\game_save_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\game_save_diff.exe" game_save_diff.exe >nul
"%TEMP%\game_save_diff.exe"
