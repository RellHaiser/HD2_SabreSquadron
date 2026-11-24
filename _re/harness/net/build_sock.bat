@echo off
rem Net part 4a: the transport's UDP socket (net/net_socket.cpp) against the original executable's (sock_diff.cpp),
rem MinGW; runs sock_diff.exe ("sock_diff: N checks, 0 failures"). Every file is built with -include sock_fakes.h: the
rem port's Winsock and thread calls renamed to the test's fakes. The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -I%SRC% -I..\collision -include sock_fakes.h sock_diff.cpp ..\collision\original_exe.cpp %SRC%\net\net_socket.cpp %SRC%\net\net_channel.cpp %SRC%\net\bitstream.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\sock_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\sock_diff.exe" sock_diff.exe >nul
"%TEMP%\sock_diff.exe"
