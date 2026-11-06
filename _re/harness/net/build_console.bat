@echo off
rem The console commands (part 10e-i: console_test.cpp, the game faked), MinGW; runs console_test.exe
rem ("console: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% console_test.cpp console_globals.cpp %SRC%\net\net_console_commands.cpp %SRC%\game\game_state.cpp %SRC%\common\c_str.cpp -lws2_32 -o "%TEMP%\console_test.exe" || exit /b 1
copy /y "%TEMP%\console_test.exe" console_test.exe >nul
"%TEMP%\console_test.exe"
