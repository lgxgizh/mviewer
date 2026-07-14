@echo off
call "D:/msvc/VC/Auxiliary/Build/vcvarsall.bat" x64
cd /d D:/mviewer
rd /s /q build_msvc 2>nul
md build_msvc
cd build_msvc

echo [1/4] cmake configure
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="D:/QT/6.11.1/msvc2022_64" ..
if errorlevel 1 goto FAIL

echo [2/4] cmake build
cmake --build . -j
if errorlevel 1 goto FAIL

echo [3/4] core_tests
set QT_QPA_PLATFORM=offscreen
bin\core_tests.exe
if errorlevel 1 goto FAIL

echo [4/4] m3m4m5_tests
bin\test_m3m4m5.exe
if errorlevel 1 goto FAIL

echo [PASS]
exit /b 0

:FAIL
echo [FAIL] at step %ERRORLEVEL%
exit /b 1
