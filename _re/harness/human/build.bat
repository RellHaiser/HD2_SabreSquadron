@echo off
rem The human's movement (game/human_locomotion.cpp) with the real collision world, MinGW; runs human_test.exe
rem ("human: N checks, 0 failures"). Every call is compared with the original executable's (HD2_ORIGINAL_EXE:
rem ..\collision\original_exe.h), put at its base 0x400000: the test is linked at 0x20000000. Frames, the scene and
rem the driver are the fake engine (..\common\fake_engine.inc; LS3DF.dll's world matrices, HD2_GAME_DIR).
rem Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %SRC%\game\human_locomotion.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_test.exe" 2>&1
"%TEMP%\human_test.exe"
