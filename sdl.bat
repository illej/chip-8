@echo off

set VERSION=2.0.12

echo installing SDL %VERSION%
echo   downloading
curl --show-error --progress-bar https://www.libsdl.org/release/SDL2-devel-%VERSION%-VC.zip -o sdl2.zip

echo   extracting
tar -xf sdl2.zip

echo   copying files
robocopy SDL2-%VERSION%\include include\SDL2 > nul
robocopy SDL2-%VERSION%\lib\x64 lib\x64 > nul
copy lib\x64\SDL2.dll .

echo   cleaning up
rmdir /Q /S SDL2-%VERSION% > nul
del lib\x64\SDL2.dll
del sdl2.zip

echo done
