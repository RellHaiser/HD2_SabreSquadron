@echo off
rem Net part 6: the game's own console commands and their variables (net/net_console_vars.cpp) against the original
rem executable's (cvar_diff.cpp), MinGW; runs cvar_diff.exe ("cvar_diff: N checks, 0 failures"). The intermediates go
rem to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision cvar_diff.cpp console_globals.cpp ..\collision\original_exe.cpp %SRC%\net\net_console_vars.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\cvar_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\cvar_diff.exe" cvar_diff.exe >nul
"%TEMP%\cvar_diff.exe"
