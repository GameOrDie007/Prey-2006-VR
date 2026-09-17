@echo off
rem Carry your previous PreyVR install into this one, instead of setting it up
rem again from scratch.
rem
rem Takes the retail game data, vr_support.pk4, your savegames and your settings
rem from an older PreyVR-PCVR folder and puts them here. Nothing is written to
rem the old folder - it stays exactly as it is, so if this version is worse you
rem still have the one that worked.
rem
rem With no arguments it uses the newest install it finds beside this one. If
rem the old folder is somewhere else, name it:
rem
rem     Update.bat -From "D:\Games\PreyVR-PCVR-1.0.7"
rem
rem with no backslash at the end of the path - a trailing one escapes the
rem closing quote and swallows the argument.
rem
rem Use Setup.bat instead if this is your first install.

cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "tools\update.ps1" "." %*

if errorlevel 1 goto failed
echo.
pause
exit /b 0

:failed
echo.
echo   The update did not finish. The message above says why.
echo.
pause
exit /b 1
