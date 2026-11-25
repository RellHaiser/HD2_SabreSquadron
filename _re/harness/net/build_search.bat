@echo off
rem Net part 12c: the LAN search, GameSpy reporting and CD-key checks (net/net_search.cpp) against the original
rem executable's (search_diff.cpp), MinGW; runs search_diff.exe ("search_diff: N checks, 0 failures"). The intermediates
rem go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -include search_fakes.h search_diff.cpp search_globals.cpp ..\collision\original_exe.cpp %SRC%\net\net_search.cpp %SRC%\common\c_str.cpp -Wl,--image-base=0x20000000 -lws2_32 -o "%TEMP%\search_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\search_diff.exe" search_diff.exe >nul
"%TEMP%\search_diff.exe"
