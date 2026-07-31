@echo off
rem Configure + build (+ optionally test) the core on Windows with Ninja under the MSVC toolchain.
rem Usage: scripts\win-build.bat [amd64|arm64] [test]
setlocal
set "ARCH=%~1"
if "%ARCH%"=="" set "ARCH=amd64"
set "ACTION=%~2"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH echo [win-build] Visual Studio not found & exit /b 1

call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=amd64 -no_logo || exit /b 1
cd /d "%~dp0.."

set "BUILDDIR=build-%ARCH%"
cmake -B "%BUILDDIR%" -G Ninja -S . || exit /b 1
cmake --build "%BUILDDIR%" || exit /b 1
if /i "%ACTION%"=="test" ctest --test-dir "%BUILDDIR%" --output-on-failure || exit /b 1
echo [win-build] done (%ARCH% %ACTION%)
