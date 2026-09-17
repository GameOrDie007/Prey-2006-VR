@echo off
rem Put your Prey game data next to PreyVR, so the port can read it.
rem
rem Finds Prey in your Steam libraries - it is app 3970, "Prey 2006" - or in
rem the list of installed programs, and copies the .pk4 files from its base
rem folder into preybase here. If it cannot find Prey, it prints the list of
rem files and you drag them, or the base folder, onto the window.
rem
rem Nothing is downloaded and nothing is installed. The retail data is yours
rem and stays yours; this only copies it. Run it as often as you like - files
rem already in place are left alone.
rem
rem If Prey is somewhere this cannot guess, name it:
rem
rem     Setup.bat -PreyDir "D:\Games\Prey"
rem
rem with no backslash at the end of the path - a trailing one escapes the
rem closing quote and swallows the argument.

cd /d "%~dp0"

rem -ExecutionPolicy Bypass applies to this one run only. It changes no system
rem setting, and is what lets a downloaded script run without you having to
rem alter anything. Nothing here needs administrator.
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\setup.ps1" "." %*

if errorlevel 1 goto failed
echo.
pause
exit /b 0

:failed
echo.
echo   Setup did not finish. The message above says why.
echo.
pause
exit /b 1
