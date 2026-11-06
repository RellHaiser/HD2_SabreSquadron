@echo off
rem The shape of a formation (part 12b), MinGW; runs formation_test.exe ("formation: N checks, 0 failures").
rem The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% formation_test.cpp formation_globals.cpp %SRC%\game\formation.cpp %SRC%\common\chunk.cpp -o "%TEMP%\formation_test.exe" 2>&1
"%TEMP%\formation_test.exe"
