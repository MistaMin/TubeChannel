@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)
if "%~1"=="configure" (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
) else if "%~1"=="build" (
  cmake --build build
) else if "%~1"=="test" (
  ctest --test-dir build --output-on-failure
)
exit /b %errorlevel%
