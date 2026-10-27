@echo off
rem S_NetQuantization::Setup (src/net/bitstream.cpp, 0x0073dbb0) against the x87's float-precision
rem steps, with MinGW; run quant_test.exe ("... 0 failures").
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
g++ -std=c++17 -O1 -Wall -Wextra -msse2 -mfpmath=sse -DLS3D_API= -I..\..\..\src quant_test.cpp globals.cpp ..\..\..\src\net\bitstream.cpp -o quant_test.exe
