@echo off

if not exist SDL2.dll call sdl.bat

pushd build

set exe=chip-8.exe
set libs=Shell32.lib SDL2.lib SDL2main.lib
set cflags=-Zi -nologo -Fe: %exe% /I ..\include
set ldflags=/link /subsystem:console /libpath:..\lib\x64 %libs%
set source=..\src\app.c

cl %cflags% %source% %ldflags%

copy %exe% ..

popd
