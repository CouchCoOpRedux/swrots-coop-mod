@echo off
rem If the game ever stays frozen after the recorder stopped, this lets it run again.
cd /d "%~dp0"
python xbwatch.py resume
pause
