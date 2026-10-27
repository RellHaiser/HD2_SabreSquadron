@echo off
rem Builds the vehicle parameter harness with the 32-bit MinGW (see harness.cpp).
set PATH=C:\nim-2.2.6\dist\mingw32\bin;%PATH%
cd /d %~dp0
g++ -std=c++17 -O1 -Wall -Wextra -Wno-invalid-offsetof -Istub -I..\..\..\src harness.cpp ..\..\..\src\game\vehicle_params.cpp -o harness.exe
g++ -std=c++17 -O1 -Wall -Wextra -Wno-invalid-offsetof -msse2 -mfpmath=sse -Istub -I..\..\..\src apply.cpp ..\..\..\src\game\vehicle.cpp ..\..\..\src\game\vehicle_params.cpp -o apply.exe
