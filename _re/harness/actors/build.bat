@echo off
rem The actor lists (game/actors_module.cpp) with net/bitstream.cpp, MinGW; runs actors_test.exe
rem ("actors: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\actors_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% actors_test.cpp actors_globals.cpp %SRC%\game\actors_module.cpp %SRC%\net\bitstream.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\actors_math.o" -o "%TEMP%\actors_test.exe" 2>&1
"%TEMP%\actors_test.exe"
