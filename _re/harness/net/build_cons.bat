@echo off
rem Net part 5: the console's command processor (net/net_console_commands.cpp) against the original executable's
rem (cons_diff.cpp), MinGW; runs cons_diff.exe ("cons_diff: N checks, 0 failures"). Every file is built with
rem -include cons_fakes.h (the port's ShowWindow renamed to the test's fake). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -include cons_fakes.h cons_diff.cpp console_globals.cpp cons_globals.cpp ..\collision\original_exe.cpp %SRC%\net\net_console_commands.cpp %SRC%\game\game_state.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\cons_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\cons_diff.exe" cons_diff.exe >nul
"%TEMP%\cons_diff.exe"
