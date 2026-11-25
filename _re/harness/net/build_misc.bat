@echo off
rem Net part 14: the network's odds and ends (net/net_console.cpp, included by misc_diff.cpp; net/net_com.cpp) against
rem the original executable's, MinGW; runs misc_diff.exe ("misc_diff: N checks, 0 failures"). The intermediates go
rem to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision misc_diff.cpp netgame_globals.cpp ..\collision\original_exe.cpp %SRC%\net\net_com.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -o "%TEMP%\misc_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\misc_diff.exe" misc_diff.exe >nul
"%TEMP%\misc_diff.exe"
