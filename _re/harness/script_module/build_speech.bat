@echo off
rem The speech slots (part 11b: speech_test.cpp, the engine faked), MinGW; runs speech_test.exe ("speech: N checks, 0 failures").
rem The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DSPEECH_TEST -I%SRC% speech_test.cpp %SRC%\game\speech_slots.cpp %SRC%\game\path_points.cpp -o "%TEMP%\speech_test.exe" 2>&1
copy /y "%TEMP%\speech_test.exe" speech_test.exe >nul
"%TEMP%\speech_test.exe"
