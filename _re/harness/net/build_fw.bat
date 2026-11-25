@echo off
rem Net part 12b: Windows XP's firewall (net/net_firewall.cpp, included by fw_diff.cpp; net/net_create.cpp) against the
rem original executable's, MinGW; runs fw_diff.exe ("fw_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -include fw_fakes.h fw_diff.cpp ..\collision\original_exe.cpp %SRC%\net\net_create.cpp -Wl,--image-base=0x20000000 -lole32 -loleaut32 -luuid -o "%TEMP%\fw_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\fw_diff.exe" fw_diff.exe >nul
"%TEMP%\fw_diff.exe"
