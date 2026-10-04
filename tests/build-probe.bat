@echo off
rem Сборка probe.exe — зонд стилей для shell hook (VS Native Tools x64).
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /utf-8 /Fe:%~dp0probe.exe /Fo:%~dp0 %~dp0probe.c user32.lib /link /SUBSYSTEM:WINDOWS
