@echo off
rem Saves all of the running game's RAM (read only) to logs\ram_<note>_<time>.bin.gz. xemu must have been started with -s.
rem Use it when the situation you want studied is on screen (for example level 7 with player 2 in and standing next to player 1).
cd /d "%~dp0"
set /p NOTE=What is on screen (for example: level7 p2 in) ? 
python xbwatch.py ramdump "%NOTE%"
echo.
pause
