@echo off
rem Branch net-robust-datagrams: the socket and the transport given the datagrams that crash the original
rem (robust_test.cpp, part 4a's fakes of the system calls), MinGW; runs robust_test.exe ("robust: N checks, 0
rem failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -I%SRC% -include sock_fakes.h robust_test.cpp %SRC%\net\net_transport.cpp %SRC%\net\net_socket.cpp %SRC%\net\net_channel.cpp %SRC%\net\bitstream.cpp -lws2_32 -o "%TEMP%\robust_test.exe" 2>&1
if errorlevel 1 exit /b 1
"%TEMP%\robust_test.exe"
