@echo off
rem Formation part 1: the formations' shapes (game/formation.cpp) against the original executable's (fm_diff.cpp, over
rem cmd_diff.cpp's world and logger), MinGW; runs fm_diff.exe ("fm_diff: N checks, 0 failures"). The game and the engine
rem the shapes reach are logged on both sides (fm_calls.s and fm_calls.inc, from gen_cmd_calls.py fm: rerun it when the
rem link reports an undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision fm_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\game\formation.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp fm_calls.s -o "%TEMP%\fm_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\fm_diff.exe" fm_diff.exe >nul
"%TEMP%\fm_diff.exe"
