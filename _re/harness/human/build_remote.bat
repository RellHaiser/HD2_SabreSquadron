@echo off
rem The network clients' controller of other players' humans (game/remote_controller.cpp) against the original
rem executable: build_aictl.bat's harness with remote_controller.cpp linked too (HUMAN_REMOTE: remote_test.inc's tests
rem alone, on aictl_test.inc's message and stream fakes); runs human_remote_test.exe ("human: N checks, 0 failures").
rem Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
set WRAP=-Wl,--wrap=_ZN16C_CollisionWorld9AddVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld12RemoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld10MoveVolumeEP17S_CollisionVolume -Wl,--wrap=_ZN16C_CollisionWorld16SetIgnoredVolumeEP17S_CollisionVolumei -Wl,--wrap=_ZN16C_CollisionWorld17SetIgnoredVolumesEPP17S_CollisionVolumeji -Wl,--wrap=_ZN13S_MsgMoveItem4ReadER12C_NetMessage -Wl,--wrap=_ZN17S_MsgTransferItem4ReadER12C_NetMessage -Wl,--wrap=_ZN13S_MsgDropItem4ReadER12C_NetMessage -Wl,--wrap=_ZN18S_MsgRemoteRequest4ReadER12C_NetMessage -Wl,--wrap=_ZN7S_Msg234ReadER12C_NetMessage -Wl,--wrap=_ZN8S_MsgSay4ReadER12C_NetMessage -Wl,--wrap=_ZN7C_Human14ApplyRemoteHitEiiiiRK8S_vectorS2_ -Wl,--wrap=_ZN15C_HumanMovement8OnFrozenEb
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DHUMAN_BASE -DHUMAN_SELF -DHUMAN_COMBAT -DHUMAN_CONTROLLER -DHUMAN_AICTL -DHUMAN_REMOTE -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 %WRAP% %SRC%\game\human.cpp %SRC%\game\human_combat.cpp %SRC%\game\human_controller.cpp %SRC%\game\ai_controller.cpp %SRC%\game\remote_controller.cpp %SRC%\game\ai_parts.cpp %SRC%\net\bitstream.cpp %SRC%\game\human_locomotion.cpp %SRC%\game\human_movement_base.cpp %SRC%\game\human_pose_physics.cpp %SRC%\game\human_pose_ragdoll.cpp %SRC%\game\human_pose_ik.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_remote_test.exe" 2>&1
if errorlevel 1 exit /b 1
"%TEMP%\human_remote_test.exe"
