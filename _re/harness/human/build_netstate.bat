@echo off
rem The human's state for a joining network client (game/human_net.cpp) against the original's: the base harness
rem (build.bat's) with human_net.cpp and the bit stream linked and HUMAN_NETSTATE (netstate_test.inc's tests alone).
rem What human_net.o calls besides the bit stream is wrapped (netstate_wrap.rsp: ld --wrap) into the test's loggers.
rem Runs human_netstate_test.exe ("human: N checks, 0 failures"). Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DHUMAN_NETSTATE -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 @netstate_wrap.rsp %SRC%\game\human_net.cpp %SRC%\net\bitstream.cpp %SRC%\game\human_locomotion.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_netstate_test.exe" 2>&1
if errorlevel 1 exit /b 1
"%TEMP%\human_netstate_test.exe"
