@echo off
rem Junkers part 1: the Junkers 88 bomber and its fighters (game/junkers88.cpp) against the original executable's
rem (jk_diff.cpp, over cmd_diff.cpp's world and logger), MinGW; runs jk_diff.exe ("jk_diff: N checks, 0 failures"). The
rem game and the engine the bomber reaches are logged on both sides (jk_calls.s and jk_calls.inc, from
rem gen_cmd_calls.py jk: rerun it when the link reports an undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision jk_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\game\junkers88.cpp %SRC%\game\resource_cache.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp jk_calls.s -o "%TEMP%\jk_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\jk_diff.exe" jk_diff.exe >nul
"%TEMP%\jk_diff.exe"
