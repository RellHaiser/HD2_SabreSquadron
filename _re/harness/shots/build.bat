@echo off
rem The shots (game/shots.cpp; part 1: the bullets), MinGW; runs shots_test.exe ("shots: N checks, 0 failures").
rem The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% shots_test.cpp shots_globals.cpp %SRC%\game\shots.cpp -o "%TEMP%\shots_test.exe" 2>&1
"%TEMP%\shots_test.exe"
