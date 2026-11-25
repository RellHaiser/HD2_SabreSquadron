@echo off
rem Branch net-forwarded-messages: a server's forwarded object messages cleared by force (fwd_test.cpp, net_object.cpp
rem with repl_fakes.h's counter), MinGW; runs fwd_test.exe ("fwd: N checks, 0 failures"). The intermediates go to
rem %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -include repl_fakes.h fwd_test.cpp %SRC%\net\net_object.cpp %SRC%\net\net_track.cpp %SRC%\net\bitstream.cpp -o "%TEMP%\fwd_test.exe" 2>&1
if errorlevel 1 exit /b 1
"%TEMP%\fwd_test.exe"
