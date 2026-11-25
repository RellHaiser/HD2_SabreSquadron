@echo off
rem Net part 11b: the network game's DirectPlay session (net/net_session.cpp) against the original executable's (sess_diff.cpp), MinGW;
rem runs sess_diff.exe ("sess_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -include sess_fakes.h sess_diff.cpp sess_globals.cpp ..\collision\original_exe.cpp %SRC%\net\net_game.cpp %SRC%\net\net_session.cpp %SRC%\net\net_com.cpp %SRC%\net\bitstream.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -lwinmm -lws2_32 -lole32 -o "%TEMP%\sess_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\sess_diff.exe" sess_diff.exe >nul
"%TEMP%\sess_diff.exe"
