@echo off
setlocal EnableExtensions
chcp 65001 >nul

set "PROJECT_ROOT=%~dp0"
set "PROJECT_ROOT=%PROJECT_ROOT:~0,-1%"

echo ============================================
echo  NipsEngine vcpkg dependency install
echo ============================================
echo.
echo Project root:
echo 프로젝트 루트:
echo   %PROJECT_ROOT%
echo.

if not exist "%PROJECT_ROOT%\vcpkg.json" (
    echo ERROR: vcpkg.json was not found in the project root.
    echo 오류: 프로젝트 루트에서 vcpkg.json을 찾을 수 없습니다.
    echo Run this batch file from the repository root, or keep it next to vcpkg.json.
    echo 이 배치 파일을 repo 루트에서 실행하거나 vcpkg.json 옆에 두고 실행해주세요.
    echo.
    pause
    exit /b 1
)

echo Select the parent folder where the vcpkg folder should be located.
echo vcpkg 폴더를 만들 부모 폴더를 선택해주세요.
echo This script will use or create:
echo 이 스크립트는 아래 경로를 사용하거나 생성합니다:
echo   [selected folder]\vcpkg
echo.

set "VCPKG_PARENT_DIR="
for /f "usebackq delims=" %%i in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$shell = New-Object -ComObject Shell.Application; $folder = $shell.BrowseForFolder(0, 'Select parent folder for vcpkg / vcpkg 부모 폴더 선택', 0, 0); if ($folder) { $folder.Self.Path }"`) do set "VCPKG_PARENT_DIR=%%i"

if not defined VCPKG_PARENT_DIR (
    echo vcpkg parent folder selection was canceled.
    echo vcpkg 부모 폴더 선택이 취소되었습니다.
    echo.
    pause
    exit /b 1
)

set "VCPKG_DIR=%VCPKG_PARENT_DIR%\vcpkg"
set "VCPKG_EXE=%VCPKG_DIR%\vcpkg.exe"

echo Selected parent folder:
echo 선택한 부모 폴더:
echo   %VCPKG_PARENT_DIR%
echo vcpkg root:
echo vcpkg 루트:
echo   %VCPKG_DIR%

if not exist "%VCPKG_EXE%" if exist "%VCPKG_DIR%\bootstrap-vcpkg.bat" (
    echo.
    echo vcpkg.exe was not found, but bootstrap-vcpkg.bat exists.
    echo vcpkg.exe는 없지만 bootstrap-vcpkg.bat 파일이 있습니다.
    echo Bootstrapping vcpkg...
    echo vcpkg bootstrap을 실행합니다...
    call "%VCPKG_DIR%\bootstrap-vcpkg.bat"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to bootstrap vcpkg.
        echo 오류: vcpkg bootstrap에 실패했습니다.
        pause
        exit /b 1
    )
)

if not exist "%VCPKG_EXE%" (
    echo.
    echo vcpkg.exe was not found:
    echo vcpkg.exe를 찾을 수 없습니다:
    echo   %VCPKG_EXE%
    echo.
    choice /c YN /n /m "Install vcpkg into this vcpkg folder? / 이 vcpkg 폴더에 설치할까요? [Y/N] "
    if errorlevel 2 (
        echo.
        echo Canceled.
        echo 취소되었습니다.
        pause
        exit /b 1
    )

    echo.
    echo Installing vcpkg...
    echo vcpkg를 설치합니다...
    if not exist "%VCPKG_PARENT_DIR%" mkdir "%VCPKG_PARENT_DIR%"
    git clone https://github.com/microsoft/vcpkg "%VCPKG_DIR%"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to clone vcpkg.
        echo 오류: vcpkg clone에 실패했습니다.
        pause
        exit /b 1
    )

    call "%VCPKG_DIR%\bootstrap-vcpkg.bat"
    if errorlevel 1 (
        echo.
        echo ERROR: Failed to bootstrap vcpkg.
        echo 오류: vcpkg bootstrap에 실패했습니다.
        pause
        exit /b 1
    )
)

if not exist "%VCPKG_EXE%" (
    echo.
    echo ERROR: vcpkg.exe still was not found after setup.
    echo 오류: setup 후에도 vcpkg.exe를 찾을 수 없습니다.
    echo   %VCPKG_EXE%
    pause
    exit /b 1
)

echo.
echo Installing dependencies from:
echo 아래 vcpkg.json 기준으로 의존성을 설치합니다:
echo   %PROJECT_ROOT%\vcpkg.json
echo.
echo vcpkg_installed will be created or updated under:
echo vcpkg_installed 폴더는 아래 프로젝트 루트 위치에 생성되거나 갱신됩니다:
echo   %PROJECT_ROOT%\vcpkg_installed
echo.

pushd "%PROJECT_ROOT%"
"%VCPKG_EXE%" install --triplet x64-windows
set "INSTALL_RESULT=%ERRORLEVEL%"
popd

if not "%INSTALL_RESULT%"=="0" (
    echo.
    echo ERROR: vcpkg install failed.
    echo 오류: vcpkg install에 실패했습니다.
    pause
    exit /b %INSTALL_RESULT%
)

echo.
echo Checking expected dependency files...
echo 설치된 의존성 파일을 확인합니다...

set "LUAJIT_HPP=%PROJECT_ROOT%\vcpkg_installed\x64-windows\include\luajit\lua.hpp"
set "LUAJIT_LIB=%PROJECT_ROOT%\vcpkg_installed\x64-windows\debug\lib\lua51.lib"
set "LUAJIT_DLL=%PROJECT_ROOT%\vcpkg_installed\x64-windows\debug\bin\lua51.dll"
set "MINIAUDIO_H=%PROJECT_ROOT%\vcpkg_installed\x64-windows\include\miniaudio.h"

if not exist "%LUAJIT_HPP%" echo WARNING: Missing / 경고: 파일 없음 %LUAJIT_HPP%
if not exist "%LUAJIT_LIB%" echo WARNING: Missing / 경고: 파일 없음 %LUAJIT_LIB%
if not exist "%LUAJIT_DLL%" echo WARNING: Missing / 경고: 파일 없음 %LUAJIT_DLL%
if not exist "%MINIAUDIO_H%" echo WARNING: Missing / 경고: 파일 없음 %MINIAUDIO_H%

if exist "%LUAJIT_HPP%" if exist "%LUAJIT_LIB%" if exist "%LUAJIT_DLL%" if exist "%MINIAUDIO_H%" (
    echo.
    echo NipsEngine dependencies install completed successfully.
    echo NipsEngine 의존성 설치가 완료되었습니다.
)

echo.
echo Build with Visual Studio configuration: Debug ^| x64
echo Visual Studio에서 Debug ^| x64 구성으로 빌드하세요.
echo.
pause
