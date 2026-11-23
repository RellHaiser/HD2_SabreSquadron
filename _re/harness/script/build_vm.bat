@echo off
rem Script part 2: the mission script interpreter against the original executable's (vm_diff.cpp), MinGW; runs
rem vm_diff.exe ("vm_diff: N checks, 0 failures"). The game the interpreter reaches is link stubs (vm_stubs.s, from
rem gen_vm_stubs.py: rerun it when the link reports an undefined reference). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-stringop-overread -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision vm_diff.cpp sc_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 -Wl,--wrap=_ZN8C_Script12CallFunctionEiPKiS1_ %SRC%\script\script_compiler.cpp %SRC%\script\script_expression.cpp %SRC%\script\script_tables.cpp %SRC%\script\script_tables_lookup.cpp %SRC%\script\script.cpp %SRC%\script\script_vm.cpp %SRC%\script\script_functions.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp vm_stubs.s -o "%TEMP%\vm_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\vm_diff.exe" vm_diff.exe >nul
"%TEMP%\vm_diff.exe"
