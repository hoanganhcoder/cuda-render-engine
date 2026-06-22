@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
set "BUILD_DIR=%REPO_ROOT%\build_vs"
set "BUILD_TYPE=Release"
set "GENERATOR="
set "BUILD_WHEEL=0"
set "CMAKE_ARGS="

:parse_args
if "%~1"=="" goto after_args
if /I "%~1"=="--debug" (
  set "BUILD_TYPE=Debug"
  shift
  goto parse_args
)
if /I "%~1"=="--release" (
  set "BUILD_TYPE=Release"
  shift
  goto parse_args
)
if /I "%~1"=="--ninja" (
  set "GENERATOR=Ninja"
  set "BUILD_DIR=%REPO_ROOT%\build_ninja"
  shift
  goto parse_args
)
if /I "%~1"=="--vs" (
  set "GENERATOR=Visual Studio 17 2022"
  set "BUILD_DIR=%REPO_ROOT%\build_vs"
  shift
  goto parse_args
)
if /I "%~1"=="--wheel" (
  set "BUILD_WHEEL=1"
  shift
  goto parse_args
)
if /I "%~1"=="--clean" (
  if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
  shift
  goto parse_args
)
set "CMAKE_ARGS=%CMAKE_ARGS% %~1"
shift
goto parse_args

:after_args
where cmake >nul 2>nul
if errorlevel 1 (
  echo [ERROR] CMake was not found in PATH.
  exit /b 1
)

where python >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Python was not found in PATH.
  exit /b 1
)

if not defined GENERATOR (
  where ninja >nul 2>nul
  if errorlevel 1 (
    set "GENERATOR=Visual Studio 17 2022"
    set "BUILD_DIR=%REPO_ROOT%\build_vs"
  ) else (
    set "GENERATOR=Ninja"
    set "BUILD_DIR=%REPO_ROOT%\build_ninja"
  )
)

echo [INFO] Repo root  : %REPO_ROOT%
echo [INFO] Build dir  : %BUILD_DIR%
echo [INFO] Build type : %BUILD_TYPE%
echo [INFO] Generator  : %GENERATOR%

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %CMAKE_ARGS%
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel
if errorlevel 1 exit /b 1

if "%BUILD_WHEEL%"=="1" (
  echo [INFO] Building wheel...
  pushd "%REPO_ROOT%"
  python -m pip install --upgrade pip setuptools wheel build
  if errorlevel 1 (
    popd
    exit /b 1
  )
  python -m build --wheel
  if errorlevel 1 (
    popd
    exit /b 1
  )
  popd
)

echo.
echo [OK] Build complete.
echo [OK] Native module should be under:
echo      %BUILD_DIR%
if "%BUILD_WHEEL%"=="1" (
  echo [OK] Wheel output should be under:
  echo      %REPO_ROOT%\dist
)

endlocal
