@echo off
rem Human part 65: the squad members' panels (game/interface_members.cpp) against the original executable's
rem (members_diff.cpp), MinGW; runs members_diff.exe ("members_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision members_diff.cpp interface_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\common\c_str.cpp -o "%TEMP%\members_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\members_diff.exe" members_diff.exe >nul
"%TEMP%\members_diff.exe"
