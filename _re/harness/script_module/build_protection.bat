@echo off
rem The copy protection's pass-through and a frame's data (part 11d), MinGW; runs protection_test.exe ("protection: N checks, 0 failures").
rem The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% protection_test.cpp %SRC%\game\copy_protection.cpp -o "%TEMP%\protection_test.exe" 2>&1
copy /y "%TEMP%\protection_test.exe" protection_test.exe >nul
"%TEMP%\protection_test.exe"
