@echo off
rem Human part 59: the server list and its rows (game/controls.cpp) against the original executable's (server_diff.cpp with
rem controls_kit.inc), MinGW; runs server_diff.exe ("server_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision server_diff.cpp controls_data.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\game\controls.cpp %SRC%\game\control.cpp %SRC%\common\c_str.cpp %SRC%\game\mp_text_ids.cpp -o "%TEMP%\server_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\server_diff.exe" server_diff.exe >nul
"%TEMP%\server_diff.exe"
