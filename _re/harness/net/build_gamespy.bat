@echo off
rem The GameSpy SDK (part 10f: gamespy_test.cpp; loopback UDP on port 29910, TCP on 28910), MinGW; runs gamespy_test.exe
rem ("gamespy: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DGAMESPY_QR2 -I%SRC% gamespy_test.cpp %SRC%\net\gamespy_util.cpp %SRC%\net\gamespy_gcd.cpp %SRC%\net\gamespy_qr2.cpp %SRC%\net\gamespy_browser.cpp -lws2_32 -o "%TEMP%\gamespy_test.exe" || exit /b 1
copy /y "%TEMP%\gamespy_test.exe" gamespy_test.exe >nul
"%TEMP%\gamespy_test.exe"
