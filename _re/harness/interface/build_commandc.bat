@echo off
rem Human part 74c: the command map's mouse and input (game/interface_command.cpp) against the
rem original executable's (commandc_diff.cpp), MinGW; nothing patched any more (the flags kept as in 74a and 74b: no inlining of
rem once-called functions and no interprocedural assumptions about them (modref, pure-const, reference); runs commandc_diff.exe ("commandc_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -fno-inline-functions-called-once -fno-ipa-modref -fno-ipa-pure-const -fno-ipa-reference -fno-ipa-reference-addressable -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision commandc_diff.cpp interface_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\common\c_str.cpp %SRC%\common\radix_sort.cpp -o "%TEMP%\commandc_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\commandc_diff.exe" commandc_diff.exe >nul
"%TEMP%\commandc_diff.exe"
