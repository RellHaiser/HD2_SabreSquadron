@echo off
rem Vehicle physics part 2 on: the vehicles (game/vehicle*.cpp) against the original executable's (vh_diff.cpp, over
rem cmd_diff.cpp's world and logger), MinGW; runs vh_diff.exe ("vh_diff: N checks, 0 failures"). The game and the engine
rem the vehicles reach are logged on both sides (vh_calls.s and vh_calls.inc, from gen_cmd_calls.py vh: rerun it when the
rem link reports an undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -include vh_fakes.h vh_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\script\actor_scripts.cpp %SRC%\game\vehicle.cpp %SRC%\game\vehicle_collide.cpp %SRC%\game\vehicle_damage.cpp %SRC%\game\vehicle_drive.cpp %SRC%\game\vehicle_drivetrain.cpp %SRC%\game\vehicle_effects.cpp %SRC%\game\vehicle_lamps.cpp %SRC%\game\vehicle_parts.cpp %SRC%\game\vehicle_setup.cpp %SRC%\game\vehicle_skid_marks.cpp %SRC%\game\vehicle_solve.cpp %SRC%\game\vehicle_sounds.cpp %SRC%\game\vehicle_start.cpp %SRC%\game\vehicle_state.cpp %SRC%\game\vehicle_update.cpp %SRC%\game\vehicle_wheels.cpp %SRC%\game\vehicle_params.cpp %SRC%\game\car_tables.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp vh_calls.s -o "%TEMP%\vh_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\vh_diff.exe" vh_diff.exe >nul
"%TEMP%\vh_diff.exe"
