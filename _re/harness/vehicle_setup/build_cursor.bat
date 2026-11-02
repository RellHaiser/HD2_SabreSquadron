@echo off
rem Builds the cursor harness (part 9c-vi-a: src/game/cursor.cpp alone on fake engine objects, cursor_test.cpp) with the 32-bit MinGW.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -I..\..\..\src cursor_test.cpp ..\..\..\src\game\cursor.cpp -o harness_cursor.exe
