@echo off
rem Net part 3: the transport's packets, pools, channels and messages (net/net_channel.cpp) against the original
rem executable's (chan_diff.cpp), MinGW; runs chan_diff.exe ("chan_diff: N checks, 0 failures"). The intermediates go
rem to %TEMP%. The port's free is wrapped (--wrap=free) to see its pool's big buffers freed.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -I%SRC% -I..\collision chan_diff.cpp ..\collision\original_exe.cpp %SRC%\net\net_channel.cpp %SRC%\net\bitstream.cpp -Wl,--image-base=0x20000000 -Wl,--wrap=free -lws2_32 -o "%TEMP%\chan_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\chan_diff.exe" chan_diff.exe >nul
"%TEMP%\chan_diff.exe"
