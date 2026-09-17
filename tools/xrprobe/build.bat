@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo VCVARS FAILED & exit /b 1)
cd /d "%~dp0"
cl /nologo /W3 /EHsc /I "..\..\app\src\main\jni\OpenXR" probe.cpp /Fe:xrprobe.exe /link opengl32.lib user32.lib gdi32.lib
exit /b %errorlevel%
