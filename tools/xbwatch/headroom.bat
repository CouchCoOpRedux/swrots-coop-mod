@echo off
rem Prints the free-memory table of the headroom diagnostic build (xemu must have been started with -s) and appends it to logs\headroom.txt.
cd /d "%~dp0"
python xbwatch.py headroom
echo.
pause
