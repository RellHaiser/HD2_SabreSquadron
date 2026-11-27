@echo off
rem Checkpoint search part 1: the checkpoint networks' reachability queries, cover picking and path searches
rem (game/checkpoint_search.cpp) against the original executable's (cs_diff.cpp, over cn_diff.cpp's networks and
rem cmd_diff.cpp's world and logger), MinGW; runs cs_diff.exe ("cs_diff: N checks, 0 failures") on the shipped missions'
rem check2.bin (HD2_UNPACKED, default H:\HD2_Unpacked) and the test's networks. The game and the engine the networks
rem reach are logged on both sides (cs_calls.s and cs_calls.inc, from gen_cmd_calls.py cs: rerun it when the link
rem reports an undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision cs_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\game\checkpoint_network.cpp %SRC%\game\checkpoint.cpp %SRC%\game\checkpoints.cpp %SRC%\game\checkpoint_search.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp cs_calls.s -o "%TEMP%\cs_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\cs_diff.exe" cs_diff.exe >nul
"%TEMP%\cs_diff.exe"
