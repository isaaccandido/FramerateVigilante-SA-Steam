@echo off
rem Builds FramerateVigilanteSteam.asi (32-bit) with Zig: https://ziglang.org/download/
rem   or: pip install ziglang   (then use "python -m ziglang cc" instead of "zig cc")
if not exist build mkdir build
zig cc -target x86-windows-gnu -O2 -shared -Wall -Wextra -o build\FramerateVigilanteSteam.asi src\main.c
if errorlevel 1 exit /b 1
echo Built build\FramerateVigilanteSteam.asi
