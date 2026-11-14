@echo off
rem The human's sight, hearing and events (game/human_senses.cpp, game/human_events.cpp) against the original
rem executable: build_control.bat's harness with them linked too (HUMAN_SENSES: senses_test.inc's tests alone);
rem IsFriendOf, IsDisguisedEnemy and UpdateDisguise scripted through the linker's --wrap. Runs human_senses_test.exe
rem ("human: N checks, 0 failures"). Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
set WRAP=-Wl,--wrap=_ZN16C_CollisionWorld9AddVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld12RemoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld10MoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld16SetIgnoredVolumeEP17S_CollisionVolumei -Wl,--wrap=_ZN16C_CollisionWorld17SetIgnoredVolumesEPP17S_CollisionVolumeji
set WRAP2=-Wl,--wrap=_ZNK7C_Human10IsFriendOfEPK7C_Actor -Wl,--wrap=_ZNK7C_Human16IsDisguisedEnemyEPKS_ -Wl,--wrap=_ZN7C_Human14UpdateDisguiseEf
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DHUMAN_BASE -DHUMAN_SELF -DHUMAN_COMBAT -DHUMAN_CONTROL -DHUMAN_SENSES -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %WRAP% %WRAP2% %SRC%\game\human.cpp %SRC%\game\human_combat.cpp %SRC%\game\human_control.cpp %SRC%\game\human_senses.cpp %SRC%\game\human_events.cpp %SRC%\game\human_locomotion.cpp %SRC%\game\human_movement_base.cpp %SRC%\game\human_pose_physics.cpp %SRC%\game\human_pose_ragdoll.cpp %SRC%\game\human_pose_ik.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_senses_test.exe" 2>&1
if errorlevel 1 exit /b 1
"%TEMP%\human_senses_test.exe"
