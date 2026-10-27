@echo off
rem Builds the checkpoint networks' test harness with the 32-bit MinGW of the Nim install and runs
rem it on every mission of the unpacked game (it writes joins.bin and joins_bad.bin in .\run). SSE
rem math as MSVC compiles it: x87 extended precision would not round the cells' products to floats.
rem   build.bat            build and run
rem   build.bat nobuild    run only
setlocal
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
set SRC=%~dp0..\..\..\src
set HERE=%~dp0
if /I not "%1"=="nobuild" (
  g++ -std=c++17 -O1 -Wall -msse2 -mfpmath=sse -I"%HERE%stub" -I"%SRC%" "%HERE%harness.cpp" "%SRC%\common\chunk.cpp" ^
    "%SRC%\game\checkpoint.cpp" "%SRC%\game\checkpoint_network.cpp" "%SRC%\game\checkpoint_search.cpp" ^
    "%SRC%\game\checkpoints.cpp" -o "%HERE%harness.exe" || exit /b 1
)
if not exist "%HERE%run" mkdir "%HERE%run"
pushd "%HERE%run"
"%HERE%harness.exe" H:\HD2_Unpacked\MISSIONS
popd
