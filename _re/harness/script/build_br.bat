@echo off
rem Barrel part 1: the barrels (game/barrel.cpp) against the original executable's (br_diff.cpp, over cmd_diff.cpp's world
rem and logger), MinGW; runs br_diff.exe ("br_diff: N checks, 0 failures"). The game and the engine the barrels reach are
rem logged on both sides (br_calls.s and br_calls.inc, from gen_cmd_calls.py br: rerun it when the link reports an
rem undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision br_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\game\barrel.cpp %SRC%\game\resource_cache.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp br_calls.s -o "%TEMP%\br_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\br_diff.exe" br_diff.exe >nul
"%TEMP%\br_diff.exe"
