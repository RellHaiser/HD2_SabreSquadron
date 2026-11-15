@echo off
rem The humans' animation tables (game/human_anim_tables.cpp) against the original's: the base harness (build.bat's)
rem with human_anim_tables.cpp linked and HUMAN_ANIMTABLES (animtables_test.inc's tests alone; Tables\anim.sav from
rem H:\HD2_Unpacked). The animation cache's Get and ShowMessage are wrapped (animtables_wrap.rsp: ld --wrap).
rem Runs human_animtables_test.exe ("human: N checks, 0 failures"). Intermediates go to %TEMP%.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -Wno-array-bounds -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DHUMAN_ANIMTABLES -I%SRC% -I..\collision -I..\common human_test.cpp human_globals.cpp animtables_globals.cpp ..\collision\original_exe.cpp -Wl,--image-base=0x20000000 @animtables_wrap.rsp %SRC%\game\human_anim_tables.cpp %SRC%\game\human_locomotion.cpp %SRC%\game\collision.cpp %SRC%\game\collision_body.cpp %SRC%\game\collision_ray.cpp %SRC%\game\collision_sweep.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_math.o" -o "%TEMP%\human_animtables_test.exe" 2>&1
if errorlevel 1 exit /b 1
"%TEMP%\human_animtables_test.exe"
