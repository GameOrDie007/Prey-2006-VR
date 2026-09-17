@echo off
rem ===================================================================
rem  OpenXR frame timing probe - WITH A REAL LOAD
rem
rem  Still not the game. But unlike the blank-frame run, this actually
rem  draws: 4000 random triangles per eye, new ones every frame, into
rem  the full eye buffer.
rem
rem  That is deliberately hostile to a video encoder - high entropy and
rem  nothing it can predict from the previous frame - and it costs real
rem  fill rate at your headset's resolution. It is the half of the
rem  workload the blank run was missing.
rem
rem  If THIS stalls and the blank run did not, the problem needs load to
rem  appear, and no application could have avoided it either.
rem ===================================================================

setlocal
cd /d "%~dp0"

echo.
echo   Headset on and streaming. Two minutes, drawing hard.
echo   Leave the headset still - put it down if you like.
echo.
pause

.\xrtiming.exe 120 4000

echo.
echo   Saved to timing-report.txt next to this file.
echo.
pause
endlocal
