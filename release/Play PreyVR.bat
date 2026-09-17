@echo off
rem ===================================================================
rem  PreyVR - PCVR
rem
rem  Start your OpenXR runtime (Virtual Desktop, SteamVR, Oculus) and
rem  put the headset on FIRST, then run this.
rem
rem  logFile 2 writes saves\preybase\qconsole.log as you play, so if
rem  anything goes wrong there is a record of it. Nothing else is added.
rem ===================================================================

setlocal
cd /d "%~dp0"

.\PreyVR.exe +set logFile 2

endlocal
