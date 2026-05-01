@echo off
setlocal EnableExtensions

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

echo ============================================
echo  Lua 5.4.8 vcpkg install
echo ============================================
echo.
echo Project root:
echo   %PROJECT_ROOT%
echo.

if not exist "%PROJECT_ROOT%\vcpkg.json" (
    echo ERROR: vcpkg.json was not found in the project root.
    echo Run this batch file from the repository root, or keep it next to vcpkg.json.
    echo.
    pause
    exit /b 1
)

echo vcpkg를 설치할 폴더를 선택해주세요.
echo 만약 vcpkg가 설치되어 있지 않다면, 이 스크립트가 해당 폴더에 vcpkg를 설치해 줍니다.
.echo.

set "VCPKG_DIR="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$shell = New-Object -ComObject Shell.Application; $folder = $shell.BrowseForFolder(0, 'Select vcpkg installation folder', 0, 0); if ($folder) { $folder.Self.Path }"`) do set "VCPKG_DIR=%%i"

if not defined VCPKG_DIR (
    echo vcpkg folder selection was canceled.
    echo.
    pause
    exit /b 1
)

set "VCPKG_EXE=%VCPKG_DIR%\vcpkg.exe"

if not exist "%VCPKG_EXE%" if exist "%VCPKG_DIR%\bootstrap-vcpkg.bat" (
    echo.
    echo vcpkg.exe was not found, but bootstrap-vcpkg.bat exists.
    echo Bootstrapping vcpkg...
    call "%VCPKG_DIR%\bootstrap-vcpkg.bat"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to bootstrap vcpkg.
        pause
        exit /b 1
    )
)

if not exist "%VCPKG_EXE%" (
    echo.
    echo vcpkg.exe was not found:
    echo   %VCPKG_EXE%
    echo.
    choice /c YN /n /m "Install vcpkg into this folder? [Y/N] "
    if errorlevel 2 (
        echo.
        echo Canceled.
        pause
        exit /b 1
    )

    echo.
    echo Installing vcpkg...
    git clone https://github.com/microsoft/vcpkg "%VCPKG_DIR%"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to clone vcpkg.
        pause
        exit /b 1
    )

    call "%VCPKG_DIR%\bootstrap-vcpkg.bat"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to bootstrap vcpkg.
        pause
        exit /b 1
    )
)

if not exist "%VCPKG_EXE%" (
    echo.
    echo ERROR: vcpkg.exe still was not found after setup.
    echo   %VCPKG_EXE%
    pause
    exit /b 1
)

echo.
echo Installing dependencies from:
echo   %PROJECT_ROOT%\vcpkg.json
echo.
echo vcpkg_installed will be created or updated under:
echo   %PROJECT_ROOT%\vcpkg_installed
echo.

pushd "%PROJECT_ROOT%"
"%VCPKG_EXE%" install --triplet x64-windows
set "INSTALL_RESULT=%ERRORLEVEL%"
popd

if not "%INSTALL_RESULT%"=="0" (
    echo.
    echo ERROR: vcpkg install failed.
    pause
    exit /b %INSTALL_RESULT%
)

echo.
echo Checking expected Lua files...

set "LUA_HPP=%PROJECT_ROOT%\vcpkg_installed\x64-windows\include\lua.hpp"
set "LUA_LIB=%PROJECT_ROOT%\vcpkg_installed\x64-windows\debug\lib\lua.lib"
set "LUA_DLL=%PROJECT_ROOT%\vcpkg_installed\x64-windows\debug\bin\lua.dll"

if not exist "%LUA_HPP%" echo WARNING: Missing %LUA_HPP%
if not exist "%LUA_LIB%" echo WARNING: Missing %LUA_LIB%
if not exist "%LUA_DLL%" echo WARNING: Missing %LUA_DLL%

if exist "%LUA_HPP%" if exist "%LUA_LIB%" if exist "%LUA_DLL%" (
    echo.
    echo Lua 5.4.8 install completed successfully.
)

echo.
echo Build with Visual Studio configuration: Debug ^| x64
echo.
pause
