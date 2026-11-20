@echo off
rem Human part 73a: the inventory's scroll bars (game/interface_inventory.cpp) against the original executable's
rem (scrollbar_diff.cpp), MinGW; runs scrollbar_diff.exe ("scrollbar_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision scrollbar_diff.cpp interface_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\common\c_str.cpp %SRC%\common\radix_sort.cpp -o "%TEMP%\scrollbar_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\scrollbar_diff.exe" scrollbar_diff.exe >nul
"%TEMP%\scrollbar_diff.exe"
