# chip-8
Simple [CHIP-8](https://en.wikipedia.org/wiki/CHIP-8) emulator in C using [SDL2](https://www.libsdl.org/)

`build.bat` expects [portable MSVC](https://gist.github.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977) in your home directory, otherwise use a Visual Studio developer shell and comment out the first line:
```
@echo off

call "%USERPROFILE%\code\msvc\setup.bat" <--- here

if not exist SDL2.dll call sdl.bat
if not exist build mkdir build

pushd build

set exe=chip-8.exe
set libs=Shell32.lib SDL2.lib SDL2main.lib
set cflags=-Zi -nologo -Fe: %exe% /I ..\include
set ldflags=/link /subsystem:console /libpath:..\lib\x64 %libs%
set source=..\src\app.c

cl %cflags% %source% %ldflags%

copy %exe% ..

popd
```

Run with `chip-8.exe roms\<rom-name-here>`
```
chip-8>dir /B roms
bc_test.ch8
IBM_Logo.ch8
invaders.c8
pong2.c8
test_opcode.ch8
tetris.c8

chip-8>chip-8.exe roms\pong2.c8
```
