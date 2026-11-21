@echo off
rem Human part 75: the game interface's core (game/interface.cpp) against the original executable's (ui_diff.cpp: parts
rem 75a..f), MinGW; the model and sound caches real on both sides (game/resource_cache.cpp: the original's Init inlines the
rem model cache), the chunks too (common/chunk.cpp: the original's saves inline them; the data files in memory); runs
rem ui_diff.exe ("ui_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DINTERFACE_REAL_CACHES -I%SRC% -I..\collision ui_diff.cpp interface_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\common\c_str.cpp %SRC%\common\radix_sort.cpp %SRC%\game\resource_cache.cpp %SRC%\common\chunk.cpp -o "%TEMP%\ui_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\ui_diff.exe" ui_diff.exe >nul
"%TEMP%\ui_diff.exe"
