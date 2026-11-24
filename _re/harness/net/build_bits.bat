@echo off
rem Net part 1: the bit streams and the messages' encodings (net/bitstream.cpp) against the original executable's
rem (bits_diff.cpp), MinGW; runs bits_diff.exe ("bits_diff: N checks, 0 failures"). The port's malloc / realloc / free are
rem wrapped (--wrap) to log them. The intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -I%SRC% -I..\collision bits_diff.cpp ..\collision\original_exe.cpp %SRC%\net\bitstream.cpp -Wl,--image-base=0x20000000 -Wl,--wrap=malloc,--wrap=free,--wrap=realloc %MUT_LDFLAGS% -o "%TEMP%\bits_diff.exe" 2>&1
if errorlevel 1 exit /b 1
copy /y "%TEMP%\bits_diff.exe" bits_diff.exe >nul
"%TEMP%\bits_diff.exe"
