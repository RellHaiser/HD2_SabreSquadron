@echo off
rem Net part 10: the game commands (net/net_commands.cpp) against the original executable's (cmds_diff.cpp), MinGW;
rem runs cmds_diff.exe ("cmds_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -include cmds_fakes.h cmds_diff.cpp cmds_globals.cpp ..\collision\original_exe.cpp %SRC%\net\net_commands.cpp %SRC%\net\bitstream.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -lwinmm -o "%TEMP%\cmds_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\cmds_diff.exe" cmds_diff.exe >nul
"%TEMP%\cmds_diff.exe"
