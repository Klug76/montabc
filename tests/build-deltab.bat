@echo off
rem Сборка deltab.exe — окно с само-DeleteTab (VS Native Tools x64).
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /utf-8 /Fe:%~dp0deltab.exe /Fo:%~dp0 %~dp0deltab.c ole32.lib user32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS
