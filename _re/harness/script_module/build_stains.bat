@echo off
rem The blood stains on a human's body (part 11g), MinGW; runs stains_test.exe ("stains: N checks, 0 failures").
rem The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-unused-variable -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% stains_test.cpp %SRC%\game\human_stains.cpp %SRC%\common\chunk.cpp -o "%TEMP%\stains_test.exe" 2>&1
"%TEMP%\stains_test.exe"
