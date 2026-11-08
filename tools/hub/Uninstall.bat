@echo off
rem Prey VR: Uninstall.  Removes everything Setup added: PreyVR and the copy of your game data,
rem the launchers, the desktop shortcut and the Steam library entry.  Your saved games and
rem settings are kept.  Prey itself is never touched.
rem
rem Tools and hubs: run tools\setup.ps1 -Uninstall -Quiet with PowerShell for the exit code
rem (this file deletes itself, so it cannot pass one back).
rem No parenthesised blocks around paths: "(x86)" in one would end them early.
setlocal
set "SCRIPT=%~dp0tools\setup.ps1"
if not exist "%SCRIPT%" set "SCRIPT=%~dp0files\setup.ps1"
set "QUIET="
echo %* | findstr /i /c:"-Quiet" /c:"-Json" >nul && set "QUIET=1"
rem From the temp folder, so this window is not standing in the VR folder it removes; all on
rem one line, because this file is deleted as it runs, and "(goto)" ends it without reading the
rem file again (which would fail).
pushd "%TEMP%" & powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT%" -Uninstall %* & if not defined QUIET (echo. & pause) & (goto) 2>nul
