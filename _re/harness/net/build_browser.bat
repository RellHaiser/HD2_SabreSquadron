@echo off
rem Net part 13d: GameSpy's server browser (net/gamespy_browser.cpp and gamespy_util.cpp, included by browser_diff.cpp)
rem against the original executable's, MinGW; runs browser_diff.exe ("browser_diff: N checks, 0 failures"). The
rem intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-unused-function -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision browser_diff.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\browser_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\browser_diff.exe" browser_diff.exe >nul
"%TEMP%\browser_diff.exe"
