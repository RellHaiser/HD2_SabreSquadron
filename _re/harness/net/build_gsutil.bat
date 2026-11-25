@echo off
rem Net part 13a: the GameSpy SDK's utilities (net/gamespy_util.cpp, included by gsutil_diff.cpp) against the original
rem executable's, MinGW; runs gsutil_diff.exe ("gsutil_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-unused-function -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision gsutil_diff.cpp ..\collision\original_exe.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\gsutil_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\gsutil_diff.exe" gsutil_diff.exe >nul
"%TEMP%\gsutil_diff.exe"
