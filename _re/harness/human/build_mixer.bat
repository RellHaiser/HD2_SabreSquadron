@echo off
rem The human's animation mixer (game/human_anim_mixer.cpp) against the original executable: build_move.bat's harness
rem with game/human_anim_mixer.cpp linked too (HUMAN_MIXER: mixer_test.inc's tests alone); runs human_mixer_test.exe
rem ("human: N checks, 0 failures"). Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
set WRAP=-Wl,--wrap=_ZN16C_CollisionWorld9AddVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld12RemoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld10MoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld16SetIgnoredVolumeEP17S_CollisionVolumei -Wl,--wrap=_ZN16C_CollisionWorld17SetIgnoredVolumesEPP17S_CollisionVolumeji
set WRAP2=-Wl,--wrap=_ZN16C_MovementSounds4PlayEiP9I3D_framePK8S_vector -Wl,--wrap=_ZN16C_MovementSounds6ReturnEP9I3D_sound -Wl,--wrap=_Z17GetMovementSoundsv -Wl,--wrap=_ZN15C_HumanMovement8InitBaseEv -Wl,--wrap=_ZN15C_HumanMovement8DoneBaseEv
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DHUMAN_BASE -DHUMAN_MOVE -DHUMAN_MIXER -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %WRAP% %WRAP2% %SRC%\game\human_locomotion.cpp %SRC%\game\human_movement_base.cpp %SRC%\game\human_movement.cpp %SRC%\game\human_anim_mixer.cpp %SRC%\game\human_pose_physics.cpp %SRC%\game\human_pose_ragdoll.cpp %SRC%\game\human_pose_ik.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_mixer_test.exe" 2>&1
"%TEMP%\human_mixer_test.exe"
