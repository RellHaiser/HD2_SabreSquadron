@echo off
rem The explosions' wave, the timed events and the periodic links (part 11f), MinGW; runs explosion_test.exe ("explosion: N checks, 0 failures").
rem The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% explosion_test.cpp explosion_globals.cpp %SRC%\game\explosion_wave.cpp %SRC%\game\periodic_links.cpp %SRC%\common\chunk.cpp -o "%TEMP%\explosion_test.exe" 2>&1
"%TEMP%\explosion_test.exe"
