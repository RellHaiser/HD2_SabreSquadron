@echo off
rem The car tables' harness (src/game/car_tables.cpp) with MinGW; the stub shots.h comes first.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
g++ -std=c++17 -O1 -Wall -Wextra -msse2 -mfpmath=sse -DRW_DATA_API= -Istub -I..\..\..\src harness.cpp ..\..\..\src\game\car_tables.cpp -o harness.exe
