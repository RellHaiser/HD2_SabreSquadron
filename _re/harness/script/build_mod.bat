@echo off
rem Script parts 6 and 7: the script module (script_module_save.cpp, script_module.cpp) against the original
rem executable's (mod_diff.cpp, over cmd_diff.cpp's world and logger), MinGW; runs
rem mod_diff.exe ("mod_diff: N checks, 0 failures"). The game and the engine the module reaches are logged on both
rem sides (mod_calls.s and mod_calls.inc, from gen_cmd_calls.py mod: rerun it when the link reports an undefined
rem reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision mod_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\script\script_module.cpp %SRC%\script\script_module_save.cpp %SRC%\game\module.cpp %SRC%\game\resource_cache.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp mod_calls.s -o "%TEMP%\mod_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\mod_diff.exe" mod_diff.exe >nul
"%TEMP%\mod_diff.exe"
