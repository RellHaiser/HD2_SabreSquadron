@echo off
rem The controls (part 11c: controls_test.cpp, the graphics and the joysticks faked), MinGW; runs controls_test.exe
rem ("controls: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% controls_test.cpp controls_globals.cpp %SRC%\game\input_controls.cpp -o "%TEMP%\controls_test.exe" 2>&1
copy /y "%TEMP%\controls_test.exe" controls_test.exe >nul
"%TEMP%\controls_test.exe"
