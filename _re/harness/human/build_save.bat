@echo off
rem The weapon pose's saved games (game/human_weapon_pose.cpp: SaveGame, LoadGame) against the original
rem executable: build_core.bat's harness (HUMAN_CORE, HUMAN_SAVE: save_test.inc's tests alone); runs
rem human_save_test.exe ("human: N checks, 0 failures"). Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
set WRAP=-Wl,--wrap=_ZN16C_CollisionWorld9AddVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld12RemoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld10MoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld16SetIgnoredVolumeEP17S_CollisionVolumei -Wl,--wrap=_ZN16C_CollisionWorld17SetIgnoredVolumesEPP17S_CollisionVolumeji
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DHUMAN_BASE -DHUMAN_IK -DHUMAN_WEAPON -DHUMAN_BODY -DHUMAN_DEAD -DHUMAN_CORE -DHUMAN_SAVE -include harness_malloc.h -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %WRAP% %SRC%\game\human_locomotion.cpp %SRC%\game\human_movement_base.cpp %SRC%\game\human_pose_physics.cpp %SRC%\game\human_pose_ragdoll.cpp %SRC%\game\human_pose_ik.cpp %SRC%\game\human_pose_weapon.cpp %SRC%\game\human_pose_hold.cpp %SRC%\game\human_pose_body.cpp %SRC%\game\human_pose_seats.cpp %SRC%\game\human_pose_dead.cpp %SRC%\game\human_weapon_pose.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_save_test.exe" 2>&1
"%TEMP%\human_save_test.exe"
