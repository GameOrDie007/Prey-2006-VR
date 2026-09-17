@echo off
rem Gather everything needed to diagnose a crash, a freeze or a bad frame rate
rem into one zip, so it can be attached to a bug report in one go.
rem
rem It collects, if they exist:
rem
rem     version.txt                    which build this is
rem     saves\preybase\*.log           the engine log, and any test log
rem     freeze.txt                     where every thread was when it stopped
rem     crash.txt                      the stack of a crash
rem     saves\preybase\preyconfig.cfg  your settings
rem
rem None of it contains anything personal beyond your Windows and hardware
rem names. Open the zip and look before sending it if you would rather check.

cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$f=@(); foreach($p in @('version.txt','freeze.txt','crash.txt','saves\preybase\preyconfig.cfg')){ if(Test-Path $p){ $f+=$p; Write-Host ('  found ' + $p) } else { Write-Host ('  none  ' + $p) } };" ^
  "foreach($l in Get-ChildItem 'saves\preybase\*.log' -ErrorAction SilentlyContinue){ $f+=$l.FullName; Write-Host ('  found ' + $l.Name) };" ^
  "if($f.Count -eq 0){ Write-Host ''; Write-Host '  Nothing to collect - has the game been run from this folder?'; exit 1 };" ^
  "$z='PreyVR-report.zip'; if(Test-Path $z){ Remove-Item $z -Force }; Compress-Archive -Path $f -DestinationPath $z -Force;" ^
  "Write-Host ''; Write-Host ('  Written: ' + (Resolve-Path $z).Path)"

if errorlevel 1 goto failed
echo.
echo   Attach PreyVR-report.zip to your report.
echo.
pause
exit /b 0

:failed
echo.
pause
exit /b 1
