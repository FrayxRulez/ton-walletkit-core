@echo off
rem Build + run the test suite under AddressSanitizer (MSVC /fsanitize=address).
rem Usage: scripts\win-asan.bat [amd64]
setlocal
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=amd64"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH echo [win-asan] Visual Studio not found & exit /b 1

call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=amd64 -no_logo || exit /b 1
cd /d "%~dp0.."

set "BUILDDIR=build-asan-%ARCH%"
cmake -B "%BUILDDIR%" -G Ninja -S . -DTWK_SANITIZE=address -DCMAKE_BUILD_TYPE=RelWithDebInfo || exit /b 1
cmake --build "%BUILDDIR%" || exit /b 1
ctest --test-dir "%BUILDDIR%" --output-on-failure
echo [win-asan] done (exit %errorlevel%)
