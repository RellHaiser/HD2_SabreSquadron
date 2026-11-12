@echo off
rem The human's movement base (game/human_movement_base.cpp) against the original executable: build.bat's harness built
rem with HUMAN_BASE (the movement base linked, its fakes left out, base_test.inc's tests); runs human_base_test.exe
rem ("human: N checks, 0 failures"). The world's volume calls on the human's own volumes are logged on the port's side
rem through the linker's --wrap (the original's are redirected to the same loggers). Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
set WRAP=-Wl,--wrap=_ZN16C_CollisionWorld9AddVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld12RemoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld10MoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld16SetIgnoredVolumeEP17S_CollisionVolumei -Wl,--wrap=_ZN16C_CollisionWorld17SetIgnoredVolumesEPP17S_CollisionVolumeji
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DHUMAN_BASE -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %WRAP% %SRC%\game\human_locomotion.cpp %SRC%\game\human_movement_base.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_base_test.exe" 2>&1
"%TEMP%\human_base_test.exe"
