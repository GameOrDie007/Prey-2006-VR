@echo off
rem ===================================================================
rem  OpenXR frame timing probe
rem
rem  This is NOT the game. It opens an OpenXR session, submits blank
rem  frames at the display rate, and draws nothing at all. Every
rem  millisecond it reports belongs to the runtime, the driver, the
rem  link or Windows.
rem
rem  Two minutes, so the stalls can be timed against one another. The
rem  report now lists every stall over 40 ms with the second it
rem  happened and the gap since the last one:
rem
rem    evenly spaced  -> something wakes on a timer
rem    scattered      -> something is competing for the machine
rem
rem  It writes timing-report.txt beside itself however you launch it.
rem ===================================================================

setlocal
cd /d "%~dp0"

echo.
echo   Headset on and streaming. Two minutes. Nothing is drawn.
echo   Leave the headset still - put it down if you like.
echo.
pause

.\xrtiming.exe 120

echo.
echo   Saved to timing-report.txt next to this file.
echo.
pause
endlocal
