@echo off
rem Net part 8: the replicated objects (net/net_object.cpp) against the original executable's (repl_diff.cpp), MinGW;
rem runs repl_diff.exe ("repl_diff: N checks, 0 failures"). The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision -include repl_fakes.h repl_diff.cpp ..\collision\original_exe.cpp %SRC%\net\net_object.cpp %SRC%\net\net_track.cpp %SRC%\net\bitstream.cpp -Wl,--image-base=0x20000000 -o "%TEMP%\repl_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\repl_diff.exe" repl_diff.exe >nul
"%TEMP%\repl_diff.exe"
