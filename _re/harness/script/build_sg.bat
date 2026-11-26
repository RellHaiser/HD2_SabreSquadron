@echo off
rem Sight part 1: the sight module (game/sight.cpp) against the original executable's
rem (sg_diff.cpp, over cmd_diff.cpp's world and logger), MinGW; runs sg_diff.exe ("sg_diff: N checks, 0 failures"). The
rem game and the engine the module reaches are logged on both sides (sg_calls.s and sg_calls.inc, from
rem gen_cmd_calls.py sg: rerun it when the link reports an undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision %SRC%\game\sight.cpp sg_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\game\resource_cache.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp sg_calls.s -o "%TEMP%\sg_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\sg_diff.exe" sg_diff.exe >nul
"%TEMP%\sg_diff.exe"
