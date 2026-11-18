@echo off
rem Human part 47: the controls (game/input_controls.cpp) against the original executable's (controls_diff.cpp), MinGW; runs
rem controls_diff.exe ("controls_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision controls_diff.cpp controls_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\game\input_controls.cpp -o "%TEMP%\controls_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\controls_diff.exe" controls_diff.exe >nul
"%TEMP%\controls_diff.exe"
