@echo off
rem Net part 12a: the voice chat's DirectPlay Voice wrapper (net/net_voice.cpp) against the original executable's
rem (voice_diff.cpp), MinGW; runs voice_diff.exe ("voice_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision voice_diff.cpp ..\collision\original_exe.cpp %SRC%\net\net_voice.cpp %SRC%\net\net_com.cpp -Wl,--image-base=0x20000000 -lole32 -o "%TEMP%\voice_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\voice_diff.exe" voice_diff.exe >nul
"%TEMP%\voice_diff.exe"
