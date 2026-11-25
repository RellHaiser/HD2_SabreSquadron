@echo off
rem Mission loader part 1: the mission's loading (game/mission_loader.cpp) against the original executable's (ml_diff.cpp,
rem over cmd_diff.cpp's world and logger), MinGW; runs ml_diff.exe ("ml_diff: N checks, 0 failures"). The game and the
rem engine the loader reaches are logged on both sides (ml_calls.s and ml_calls.inc, from gen_cmd_calls.py ml: rerun it
rem when the link reports an undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision ml_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\game\mission_loader.cpp %SRC%\game\resource_cache.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp ml_calls.s -o "%TEMP%\ml_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\ml_diff.exe" ml_diff.exe >nul
"%TEMP%\ml_diff.exe"
