@echo off
setlocal
if "%GXX%"=="" set GXX=g++
if not exist dist mkdir dist
"%GXX%" -m32 -shared -O2 -static-libgcc -static-libstdc++ -o dist\re5_char_tweaks.asi src\dllmain.cpp
if errorlevel 1 exit /b 1
echo OK
