@echo off
rem The physics objects (game/physics*.cpp) with the real collision world, MinGW; runs physics_test.exe
rem ("physics: N checks, 0 failures"). Engine frames, the scene, the driver and the model cache are fakes; frames'
rem world matrices are LS3DF.dll's own (HD2_GAME_DIR). The objects' motion is also compared with the original
rem executable's (HD2_ORIGINAL_EXE: ..\collision\original_exe.h), put at its base 0x400000: the test is linked at
rem 0x20000000. Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\physics_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -I%SRC% -I..\collision physics_test.cpp physics_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\game\physics.cpp %SRC%\game\physics_box.cpp %SRC%\game\physics_door.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\game\module.cpp %SRC%\net\bitstream.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\physics_math.o" -o "%TEMP%\physics_test.exe" 2>&1
"%TEMP%\physics_test.exe"
