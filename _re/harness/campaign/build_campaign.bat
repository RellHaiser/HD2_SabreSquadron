@echo off
rem Human part 78: the campaign tree (game/campaign.cpp) against the original executable's (campaign_diff.cpp), MinGW;
rem the chunks real on both sides (common/chunk.cpp: the data files in memory; the shipped GameData\Gamedata0?.gdt read
rem from HD2_UNPACKED, default H:\HD2_Unpacked); runs campaign_diff.exe ("campaign_diff: N checks, 0 failures"). The
rem intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -I..\interface campaign_diff.cpp campaign_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp -o "%TEMP%\campaign_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\campaign_diff.exe" campaign_diff.exe >nul
"%TEMP%\campaign_diff.exe"
