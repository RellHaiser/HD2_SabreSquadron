@echo off
rem Net part 4b: the transport's connections and their manager (net/net_transport.cpp) against the original
rem executable's (trans_diff.cpp), MinGW; runs trans_diff.exe ("trans_diff: N checks, 0 failures"). Every file is built
rem with -include sock_fakes.h (part 4a's fakes of the system calls). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -I%SRC% -I..\collision -include sock_fakes.h trans_diff.cpp ..\collision\original_exe.cpp %SRC%\net\net_transport.cpp %SRC%\net\net_socket.cpp %SRC%\net\net_channel.cpp %SRC%\net\bitstream.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\trans_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\trans_diff.exe" trans_diff.exe >nul
"%TEMP%\trans_diff.exe"
