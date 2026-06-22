@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
set "BUILD_DIR=%REPO_ROOT%\build_vs"
set "BUILD_TYPE=Release"
set "GENERATOR="
set "BUILD_WHEEL=0"
set "CMAKE_ARGS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "CUDA_ALLOW_UNSUPPORTED=1"

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
if /I "%~1"=="--no-allow-unsupported" (
  set "CUDA_ALLOW_UNSUPPORTED=0"
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

where cl >nul 2>nul
if errorlevel 1 (
  call :load_msvc_env
  if errorlevel 1 exit /b 1
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

set "CACHE_FILE=%BUILD_DIR%\CMakeCache.txt"
if exist "%CACHE_FILE%" (
  findstr /I /C:"CMAKE_HOME_DIRECTORY:INTERNAL=%REPO_ROOT:\=/%" "%CACHE_FILE%" >nul 2>nul
  if errorlevel 1 (
    echo [WARN] Existing CMake cache was created from a different source path.
    echo [WARN] Removing stale build directory: %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%"
    mkdir "%BUILD_DIR%"
  )
)

echo %CMAKE_ARGS% | findstr /I "CMAKE_CUDA_FLAGS" >nul 2>nul
if errorlevel 1 (
  if "%CUDA_ALLOW_UNSUPPORTED%"=="1" (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_CUDA_FLAGS=--allow-unsupported-compiler"
    echo [INFO] Adding CUDA flag: --allow-unsupported-compiler
  )
)

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
exit /b 0

:load_msvc_env
echo [INFO] MSVC compiler not found in PATH. Trying to load Visual Studio build environment...

if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_INSTALL=%%~I"
  )
)

if defined VS_INSTALL (
  if exist "!VS_INSTALL!\Common7\Tools\VsDevCmd.bat" (
    call "!VS_INSTALL!\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x64
    goto verify_cl
  )
)

if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" (
  call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x64
  goto verify_cl
)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" (
  call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x64
  goto verify_cl
)
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" (
  call "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -host_arch=x64 -arch=x64
  goto verify_cl
)

echo [ERROR] Could not find a Visual Studio C++ build environment.
echo [ERROR] Install Visual Studio 2022 Build Tools with MSVC x64/x86 tools, or run this script from "x64 Native Tools Command Prompt for VS 2022".
exit /b 1

:verify_cl
where cl >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Visual Studio environment loaded, but cl.exe is still unavailable.
  echo [ERROR] Please install the MSVC C++ toolset and Windows SDK.
  exit /b 1
)
echo [INFO] MSVC environment loaded successfully.
exit /b 0
