@echo off
rem ===================================================================
rem  PreyVR - flatscreen, no headset
rem
rem  Runs the game in a window with keyboard, mouse and gamepad. No
rem  OpenXR runtime is needed or touched. Change the size below if you
rem  want; -flatrefresh sets the game tick (default 60).
rem ===================================================================

setlocal
cd /d "%~dp0"

.\PreyVR.exe -flat -flatres 1920x1080 +set logFile 2

endlocal
