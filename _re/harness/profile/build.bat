@echo off
rem Builds the profile layer's test harness with the 32-bit MinGW of the Nim install and runs its
rem tests in .\run (they write PlayersProfiles\Tester, game.cfg and *.bin files there).
rem   build.bat            build and run every test
rem   build.bat nobuild    run only
setlocal
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
set SRC=%~dp0..\..\..\src
set HERE=%~dp0
if /I not "%1"=="nobuild" (
  g++ -std=c++17 -O1 -I"%HERE%stub" -I"%SRC%" "%HERE%harness.cpp" "%SRC%\common\c_str.cpp" ^
    "%SRC%\common\chunk.cpp" "%SRC%\common\config_file.cpp" "%SRC%\common\os_files.cpp" ^
    "%SRC%\game\profile_roster.cpp" "%SRC%\game\profile_squad.cpp" "%SRC%\game\profile_misc.cpp" ^
    "%SRC%\game\key_bindings.cpp" "%SRC%\game\player_profile.cpp" -o "%HERE%harness.exe" || exit /b 1
)
if not exist "%HERE%run" mkdir "%HERE%run"
pushd "%HERE%run"
del /q profile1.sav options1.bin 2>nul
"%HERE%harness.exe" roster H:\HD2_Unpacked\Tables\HumanStore.def
"%HERE%harness.exe" names
"%HERE%harness.exe" squad H:\HD2_Unpacked\Tables\HumanStore.def
"%HERE%harness.exe" misc
"%HERE%harness.exe" player
popd
