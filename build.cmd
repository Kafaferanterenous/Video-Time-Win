@echo off
setlocal
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"

if not exist "%VSWHERE%" (
  echo Error: Visual Studio Installer's vswhere.exe was not found. 1>&2
  exit /b 1
)
if not exist "%CMAKE%" (
  echo Error: CMake was not found. 1>&2
  exit /b 1
)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo Error: Visual Studio C++ tools were not found. 1>&2
  exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
"%CMAKE%" -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%
"%CMAKE%" --build build
exit /b %errorlevel%
