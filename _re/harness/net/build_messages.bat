@echo off
rem The network game's messages (part 10d-v: netgame_test.cpp built with -DNET_SESSION_TEST -DNET_MESSAGES_TEST;
rem the session's build plus net_messages.cpp), MinGW; runs netmessages_test.exe ("netgame: N checks, 0 failures"). The intermediates go to %TEMP% (the tree is synced).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -DNET_SESSION_TEST -DNET_MESSAGES_TEST netgame_test.cpp netgame_globals.cpp %SRC%\net\net_game.cpp %SRC%\net\net_players.cpp %SRC%\net\net_search.cpp %SRC%\net\net_session.cpp %SRC%\net\net_messages.cpp %SRC%\net\net_voice.cpp %SRC%\net\net_com.cpp %SRC%\net\bitstream.cpp %SRC%\net\net_track.cpp %SRC%\net\net_object.cpp %SRC%\net\net_channel.cpp %SRC%\net\net_socket.cpp %SRC%\net\net_transport.cpp perf_fake.cpp %SRC%\common\c_str.cpp -lws2_32 -lwinmm -lole32 -o "%TEMP%\netmessages_test.exe" || exit /b 1
copy /y "%TEMP%\netmessages_test.exe" netmessages_test.exe >nul
"%TEMP%\netmessages_test.exe"
