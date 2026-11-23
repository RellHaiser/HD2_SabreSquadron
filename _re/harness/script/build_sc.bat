@echo off
rem Script part 1: the mission script compiler (script/script_compiler.cpp) against the original executable's
rem (sc_diff.cpp), MinGW; runs sc_diff.exe ("sc_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision sc_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp -o "%TEMP%\sc_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\sc_diff.exe" sc_diff.exe >nul
"%TEMP%\sc_diff.exe"
