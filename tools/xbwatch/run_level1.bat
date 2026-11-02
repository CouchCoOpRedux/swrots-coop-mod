@echo off
rem Level 1 test: records the memory of the game running in xemu (xemu must be started with the -s option).
cd /d "%~dp0"
python xbwatch.py run profiles\swrots_level1.json
echo.
echo (stopped)
pause
