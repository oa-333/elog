REM @echo off
setlocal enabledelayedexpansion enableextensions

REM create output directory
mkdir build\vc > nul

REM move object files back to local directory
move build\vc\*.obj .

REM include paths
set MYSQL_INC_PATH="C:\Program Files\MySQL\MySQL Connector C++ 9.3\include"
set MYSQL_LIB_PATH="C:\Program Files\MySQL\MySQL Connector C++ 9.3\lib64\vs14"
set SQLITE_INC_PATH="C:\Program Files\SQLite\3.49.1\inc"
set SQLITE_LIB_PATH="C:\Program Files\SQLite\3.49.1\x64"
set KAFKA_INC_PATH="C:\Program Files\librdkafka\librdkafka.redist.2.10.0\build\native\include"
set KAFKA_LIB_PATH="C:\Program Files\librdkafka\librdkafka.redist.2.10.0\build\native\lib\win\x64\win-x64-Release\v142"

REM compile
cl.exe /DELOG_ENABLE_SQLITE_DB_CONNECTOR /DELOG_ENABLE_MYSQL_DB_CONNECTOR /DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR /I %MYSQL_INC_PATH% /I %SQLITE_INC_PATH% /I %KAFKA_INC_PATH% /I src\elog\inc /std:c++20 /Zi /EHsc /MP /MDd /DELOG_DLL /c src\elog\src\*.cpp
if errorlevel 1 goto COMPILE_ERROR

REM move object files
move *.obj build\vc\
move vc140.pdb build\vc\

REM link
cd build\vc
cl.exe /Zi /EHsc /MP /MDd /LDd /Fe:elog.dll *.obj Advapi32.lib Ws2_32.lib mysqlcppconn.lib sqlite3.lib librdkafka.lib /link /LIBPATH:%MYSQL_LIB_PATH% /LIBPATH:%SQLITE_LIB_PATH% /LIBPATH:%KAFKA_LIB_PATH%
if errorlevel 1 goto LINK_ERROR
cd ..\..

REM install
copy build\vc\elog.dll bin\
copy build\vc\elog.pdb bin\
copy build\vc\elog.pdb lib\
copy build\vc\vc140.pdb bin\
copy build\vc\elog.lib lib\

set INSTALL_DIR=C:\install
mkdir %INSTALL_DIR%\elog\include\elog
copy src\elog\inc\*.h %INSTALL_DIR%\elog\include\elog
if errorlevel 1 goto INSTALL_ERROR
mkdir %INSTALL_DIR%\bin
copy bin\* %INSTALL_DIR%\bin
if errorlevel 1 goto INSTALL_ERROR
mkdir %INSTALL_DIR%\lib
copy lib\* %INSTALL_DIR%\lib
if errorlevel 1 goto INSTALL_ERROR
exit /b 0

:COMPILE_ERROR
echo "Compilation failed, aborting"
exit /b 1

:LINK_ERROR
echo "Link failed, aborting"
exit /b 2

:INSTALL_ERROR
echo "Install failed, aborting"
