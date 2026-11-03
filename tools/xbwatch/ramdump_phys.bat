@echo off
rem Saves all of the running game's physical RAM through the kernel's 0x80000000 mapping (the part ramdump.bat cannot see: textures, vertex data).
rem xemu must have been started with -s. Read only.
cd /d "%~dp0"
set /p NOTE=What is on screen (for example: level7 p2 in) ? 
python xbwatch.py ramdump "%NOTE%" --start 0x80000000
echo.
pause
