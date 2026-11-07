@echo off
rem C_ScriptModule's saved games, state and messages (part 11a: script_module_test.cpp, the engine and the game faked), MinGW;
rem runs script_module_test.exe ("script_module: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% script_module_test.cpp script_module_globals.cpp %SRC%\script\script_module_save.cpp %SRC%\common\chunk.cpp %SRC%\net\bitstream.cpp %SRC%\common\c_str.cpp -o "%TEMP%\script_module_test.exe" 2>&1
copy /y "%TEMP%\script_module_test.exe" script_module_test.exe >nul
"%TEMP%\script_module_test.exe"
