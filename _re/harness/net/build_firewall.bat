@echo off
rem Windows XP's firewall, the loading progress, the CD key (part 10d-vii: firewall_test.cpp, the NetSharingManager
rem faked), MinGW; runs firewall_test.exe ("firewall: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% firewall_test.cpp netgame_globals.cpp %SRC%\net\net_firewall.cpp %SRC%\net\net_console.cpp %SRC%\common\c_str.cpp -lws2_32 -lole32 -loleaut32 -o "%TEMP%\firewall_test.exe" || exit /b 1
copy /y "%TEMP%\firewall_test.exe" firewall_test.exe >nul
"%TEMP%\firewall_test.exe"
