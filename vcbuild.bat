REM @echo off
setlocal enabledelayedexpansion enableextensions

REM create output directory
mkdir build\vc > nul

REM move object files back to local directory
move build\vc\*.obj .

REM compile
cl.exe /I src\elog\inc /std:c++20 /Zi /EHsc /MP /MDd /c src\elog\src\*.cpp
if errorlevel 1 goto COMPILE_ERROR

REM move object files
move *.obj build\vc\
move vc140.pdb build\vc\

REM link
cd build\vc
cl.exe /Zi /EHsc /MP /MDd /LDd /Fe:elog.dll *.obj Advapi32.lib Ws2_32.lib
if errorlevel 1 goto LINK_ERROR
cd ..\..

REM install
copy build\vc\elog.dll bin\
copy build\vc\elog.pdb bin\
copy build\vc\vc140.pdb bin\
copy build\vc\elog.lib lib\
exit /b 0

COMPILE_ERROR:
echo "Compilation failed, aborting"
exit /b 1

LINK_ERROR:
echo "Link failed, aborting"
exit /b 2
