@echo off
rem Net part 7: the network game's players, teams and scores (net/net_players.cpp) against the original
rem executable's (players_diff.cpp), MinGW; runs players_diff.exe ("players_diff: N checks, 0 failures"). The
rem intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision players_diff.cpp players_globals.cpp ..\collision\original_exe.cpp %SRC%\net\net_players.cpp %SRC%\net\bitstream.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\players_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\players_diff.exe" players_diff.exe >nul
"%TEMP%\players_diff.exe"
