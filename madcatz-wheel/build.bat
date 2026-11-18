@echo off
rem Compila con Visual Studio 2022 (Build Tools) - eseguire da "x64 Native Tools Command Prompt"
cmake -S "%~dp0" -B "%~dp0build" -A x64 || goto :err
cmake --build "%~dp0build" --config Release || goto :err
echo.
echo Fatto: %~dp0build\Release\madcatz_wheel.exe
goto :eof
:err
echo Compilazione fallita.
exit /b 1
