@echo off
rem Crash reporting (part 11e), MinGW; runs crash_test.exe ("crash: N checks, 0 failures"). It faults on purpose, in child processes.
rem Runs from %TEMP%: the report goes beside the exe, and the tree is synced.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% crash_test.cpp %SRC%\game\crash_report.cpp %SRC%\common\c_str.cpp -lm -o "%TEMP%\crash_test.exe" 2>&1
"%TEMP%\crash_test.exe"
