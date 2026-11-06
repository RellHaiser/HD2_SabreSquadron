@echo off
rem The dialogue subtitles (part 13b), MinGW; runs quick_messages_test.exe ("quick messages: N checks, 0 failures").
rem The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-unused-variable -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% quick_messages_test.cpp quick_messages_globals.cpp %SRC%\game\quick_messages.cpp %SRC%\common\c_str.cpp %SRC%\common\chunk.cpp -o "%TEMP%\quick_messages_test.exe" 2>&1
"%TEMP%\quick_messages_test.exe"
