@echo off
rem Prey VR: Setup.  Finds your copy of Prey (2006) (Steam, a disc install or any folder) and
rem makes a VR folder inside it: PreyVR, a copy of your game data (about 1.7 GB) and the VR pack
rem built from it.  Your Prey itself is not changed.
rem Safe to run again: running it again is an update.  Your saves and settings are kept.
rem Drop your Prey folder on this file if it cannot find it by itself.
rem
rem Tools and hubs: Setup.bat -Quiet [-GamePath "<folder>"] [-Json]
rem Exit code 0 done, 1 nothing installed, 2 error.
rem No parenthesised blocks: a path with "(x86)" in it would end them early.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0files\setup.ps1" %*
set "RC=%ERRORLEVEL%"
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && exit /b %RC%
echo.
pause
exit /b %RC%
