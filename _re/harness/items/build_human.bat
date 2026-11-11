@echo off
rem The human's items (game/human_items.cpp) on the item system: items_test.cpp built with HUMAN_ITEMS_TEST
rem (human_items_test.inc), MinGW; runs human_items_test.exe ("human items: N checks, 0 failures"). Inputs as build.bat.
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
set SRC=..\..\..\src
g++ -std=c++17 -O1 -w -mfpmath=387 -DLS3D_API= -I..\..\..\ls3df\src -c ..\..\..\ls3df\src\math\math.cpp -o "%TEMP%\human_items_math.o" 2>&1
g++ -std=c++17 -faligned-new=8 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-invalid-offsetof -msse2 -mfpmath=sse -DLS3D_API= -DRW_DATA_API= -DHUMAN_ITEMS_TEST -I%SRC% items_test.cpp items_globals.cpp human_items_globals.cpp %SRC%\game\human_items.cpp %SRC%\game\items_module.cpp %SRC%\game\item_instances.cpp %SRC%\game\item_defs.cpp %SRC%\game\item_containers.cpp %SRC%\common\chunk.cpp %SRC%\common\c_str.cpp "%TEMP%\human_items_math.o" -o "%TEMP%\human_items_test.exe" 2>&1
"%TEMP%\human_items_test.exe"
