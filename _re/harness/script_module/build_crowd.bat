@echo off
rem The humans keeping out of each other's way (part 12a), MinGW; runs crowd_test.exe ("crowd: N checks, 0 failures").
rem The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-parentheses -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% crowd_test.cpp crowd_globals.cpp %SRC%\game\human_crowd.cpp %SRC%\common\chunk.cpp -o "%TEMP%\crowd_test.exe" 2>&1
"%TEMP%\crowd_test.exe"
