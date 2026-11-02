@echo off
rem Records the memory of the game running in xemu (xemu must be started with the -s option).
cd /d "%~dp0"
python xbwatch.py run profiles\swrots_coop.json
echo.
echo (stopped)
pause
