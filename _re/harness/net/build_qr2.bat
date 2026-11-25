@echo off
rem Net part 13c: GameSpy's query & reporting 2 (net/gamespy_qr2.cpp and gamespy_util.cpp, included by qr2_diff.cpp)
rem against the original executable's, MinGW; runs qr2_diff.exe ("qr2_diff: N checks, 0 failures"). The intermediates
rem go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-unused-function -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision qr2_diff.cpp ..\collision\original_exe.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\qr2_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\qr2_diff.exe" qr2_diff.exe >nul
"%TEMP%\qr2_diff.exe"
