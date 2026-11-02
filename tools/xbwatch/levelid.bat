@echo off
rem Reads what the co-op mod found at the start of the level you are in and appends it to logs\levelids.txt.
rem Start the level, wait about 10 seconds after it has loaded, then run this and type which level it is.
cd /d "%~dp0"
set /p NOTE=Which level is this (for example: level 2) ? 
python xbwatch.py levelid "%NOTE%"
echo.
pause
