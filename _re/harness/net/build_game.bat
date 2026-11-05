@echo off
rem The network game (part 10d: net/net_game.cpp ...) with the game faked, MinGW; runs netgame_test.exe ("netgame: N
rem checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% netgame_test.cpp netgame_globals.cpp %SRC%\net\net_game.cpp %SRC%\net\net_players.cpp %SRC%\net\net_search.cpp %SRC%\net\bitstream.cpp %SRC%\net\net_track.cpp %SRC%\net\net_object.cpp %SRC%\net\net_channel.cpp %SRC%\net\net_socket.cpp %SRC%\net\net_transport.cpp perf_fake.cpp %SRC%\common\c_str.cpp -lws2_32 -lwinmm -o "%TEMP%\netgame_test.exe" || exit /b 1
copy /y "%TEMP%\netgame_test.exe" netgame_test.exe >nul
"%TEMP%\netgame_test.exe"
