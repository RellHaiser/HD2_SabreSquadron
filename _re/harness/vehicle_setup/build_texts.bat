@echo off
rem Builds the text database's test (game_texts_test.cpp, part 9g-iii: the real game/game_texts.cpp on its own) with the 32-bit MinGW.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
g++ -std=c++17 -O1 -Wall -Wextra -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -Istub -I..\vehicle_params\stub -I..\..\..\src game_texts_test.cpp ..\..\..\src\common\c_str.cpp ..\..\..\src\game\game_texts.cpp -o harness_texts.exe
