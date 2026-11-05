@echo off
rem The net module's replication (part 10: net/net_track.cpp ...) with MinGW; run net_test.exe ("net: N checks, 0
rem failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -msse2 -mfpmath=sse -DLS3D_API= -I%SRC% net_test.cpp %SRC%\net\bitstream.cpp %SRC%\net\net_track.cpp %SRC%\net\net_object.cpp %SRC%\net\net_channel.cpp %SRC%\net\net_socket.cpp %SRC%\net\net_transport.cpp %SRC%\game\perf_timer.cpp -lws2_32 -o "%TEMP%\net_test.exe" || exit /b 1
copy /y "%TEMP%\net_test.exe" net_test.exe >nul
"%TEMP%\net_test.exe"
