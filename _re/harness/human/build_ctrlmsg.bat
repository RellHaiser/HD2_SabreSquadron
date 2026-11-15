@echo off
rem The controllers' network messages (game/controller_messages.cpp) against the original's: the base harness
rem (build.bat's) with controller_messages.cpp and the bit stream linked and HUMAN_CTRLMSG (ctrlmsg_test.inc's tests alone).
rem What controller_messages.o calls besides the bit stream is wrapped (ctrlmsg_wrap.rsp: ld --wrap) into the test's loggers.
rem Runs human_ctrlmsg_test.exe ("human: N checks, 0 failures"). Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DHUMAN_CTRLMSG -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 @ctrlmsg_wrap.rsp %SRC%\game\controller_messages.cpp %SRC%\net\bitstream.cpp %SRC%\game\human_locomotion.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_ctrlmsg_test.exe" 2>&1
if errorlevel 1 exit /b 1
"%TEMP%\human_ctrlmsg_test.exe"
