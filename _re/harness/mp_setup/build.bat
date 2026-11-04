@echo off
rem Builds the multiplayer set-up's harness (see harness.cpp) with the 32-bit MinGW of the Nim install.
setlocal
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
set SRC=%~dp0..\..\..\src
set HERE=%~dp0
g++ -std=c++17 -O1 -Wall -Wextra -I"%HERE%stub" -I"%SRC%" "%HERE%harness.cpp" "%SRC%\common\c_str.cpp" ^
  "%SRC%\common\chunk.cpp" "%SRC%\game\xml_node.cpp" "%SRC%\game\mp_game_setup.cpp" -o "%HERE%harness.exe" || exit /b 1
