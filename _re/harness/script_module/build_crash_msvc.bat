@echo off
rem Crash reporting with MSVC and a .pdb (part 11e): the real path of the call stack, symbols, lines and local variables. crash_test.exe in %TEMP%.
set VC="T:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat"
call %VC% >nul
cd /d %~dp0
set SRC=..\..\..\src
set OUT=%TEMP%\crash_msvc
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++17 /EHsc /Zi /Od /W3 /wd4996 /DLS3D_API= /DRW_DATA_API= /I%SRC% /Fo"%OUT%\\" /Fd"%OUT%\vc.pdb" /Fe"%OUT%\crash_test.exe" crash_test.cpp %SRC%\game\crash_report.cpp user32.lib /link /DEBUG 2>&1
"%OUT%\crash_test.exe"
