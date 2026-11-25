@echo off
rem Net part 13b: GameSpy's CD key client and server (net/gamespy_gcd.cpp, included by gcd_diff.cpp) against the
rem original executable's, MinGW; runs gcd_diff.exe ("gcd_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-unused-function -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision gcd_diff.cpp ..\collision\original_exe.cpp %SRC%\net\gamespy_util.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\gcd_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\gcd_diff.exe" gcd_diff.exe >nul
"%TEMP%\gcd_diff.exe"
