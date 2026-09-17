@echo off
rem ===================================================================
rem  TEST - even world motion
rem
rem  This is the candidate fix for the frame drops.
rem
rem  The game advances the world on its own clock, every 11 ms, because
rem  1000/90 is 11 in whole milliseconds. That is 90.909 updates a second
rem  against a display running at exactly 90, so the two slide past each
rem  other and some frames advance the world TWICE while others do not
rem  advance it at all.
rem
rem  In your last log that was 7.1% of frames - about six a second. It
rem  does not show up as a frame rate problem at all, which is why every
rem  measurement so far said the port was healthy.
rem
rem  This runs exactly one world update per displayed frame instead.
rem  Measured on the desk: 3774 frames, 3774 updates, no drift.
rem
rem  Side effect worth knowing: the world runs about 1.5% slower than
rem  the wall clock. It is constant, so it should be invisible - but if
rem  the game feels sluggish rather than smoother, that is what it is.
rem
rem  Compare against Play PreyVR.bat, which is unchanged.
rem ===================================================================

setlocal
cd /d "%~dp0"

echo.
echo   TEST - even world motion
echo.
echo   Load a save and play for a minute. Walk around, strafe, watch
echo   things that move - doors, lifts, enemies. That is where it shows.
echo.
echo   The question: are the frame drops better, worse, or the same?
echo.
pause

.\PreyVR.exe +set logFile 2 +set logFileName test-even-motion.log +set pcvr_frameLockedTics 1

echo.
echo   Done. Tell me how it felt - I read the log off the share.
echo.
pause
endlocal
