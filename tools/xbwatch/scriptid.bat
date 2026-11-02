@echo off
rem Reads the script recorder of the co-op mod (version 32+) and appends it to logs\scripts.txt.
rem Play the character as player two (try the powers that do not work), then run this and type who it is.
cd /d "%~dp0"
set /p NOTE=Who is player two and where (for example: Serra, level 12) ? 
python xbwatch.py scriptid "%NOTE%"
echo.
pause
