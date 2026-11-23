@echo off
rem Script part 3: the interpreter's game commands against the original executable's (cmd_diff.cpp), MinGW; runs
rem cmd_diff.exe ("cmd_diff: N checks, 0 failures"). The game and the engine the commands reach are logged on both
rem sides (cmd_calls.s and cmd_calls.inc, from gen_cmd_calls.py: rerun it when the link reports an undefined
rem reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision cmd_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp cmd_calls.s -o "%TEMP%\cmd_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\cmd_diff.exe" cmd_diff.exe >nul
"%TEMP%\cmd_diff.exe"
