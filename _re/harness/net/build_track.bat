@echo off
rem Net part 2: the replicated values (net/net_track.cpp) against the original executable's (track_diff.cpp), MinGW;
rem runs track_diff.exe ("track_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -I%SRC% -I..\collision track_diff.cpp ..\collision\original_exe.cpp %SRC%\net\net_track.cpp %SRC%\net\bitstream.cpp -Wl,--image-base=0x20000000 -o "%TEMP%\track_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\track_diff.exe" track_diff.exe >nul
"%TEMP%\track_diff.exe"
