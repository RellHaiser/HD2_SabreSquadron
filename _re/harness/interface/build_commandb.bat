@echo off
rem Human part 74b: the command map's orders, markers, lines and brackets (game/interface_command.cpp) against the
rem original executable's (commandb_diff.cpp), MinGW; the file's 74c functions are patched at run time, so no inlining of
rem once-called functions and no interprocedural assumptions about them (modref, pure-const, reference); runs commandb_diff.exe ("commandb_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -fno-inline-functions-called-once -fno-ipa-modref -fno-ipa-pure-const -fno-ipa-reference -fno-ipa-reference-addressable -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision commandb_diff.cpp interface_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\common\c_str.cpp %SRC%\common\radix_sort.cpp -o "%TEMP%\commandb_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\commandb_diff.exe" commandb_diff.exe >nul
"%TEMP%\commandb_diff.exe"
