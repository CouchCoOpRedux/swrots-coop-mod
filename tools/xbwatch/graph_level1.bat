@echo off
rem Dumps the level 1 trigger and cutscene objects (and what they point to) to logs\graph_<date>.txt.
rem Run it while the game is in level 1 (any time after the level has loaded). The game freezes for about 10 seconds.
cd /d "%~dp0"
python xbwatch.py graph --classes 0x58B3F0,0x599478,0x58C728 --ids 205EFB,206329,2063AA,205ED3,2064BF,1F0EB0 --depth 3
echo.
pause
