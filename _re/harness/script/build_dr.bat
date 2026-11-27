@echo off
rem Door part 1: the doors (game/door.cpp) against the original executable's (dr_diff.cpp, over cmd_diff.cpp's world
rem and logger), MinGW; runs dr_diff.exe ("dr_diff: N checks, 0 failures"). The game and the engine the doors reach are
rem logged on both sides (dr_calls.s and dr_calls.inc, from gen_cmd_calls.py dr: rerun it when the link reports an
rem undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision dr_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\game\door.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp dr_calls.s -o "%TEMP%\dr_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\dr_diff.exe" dr_diff.exe >nul
"%TEMP%\dr_diff.exe"
