@echo off
rem Сборка тестовой утилиты wininfo (VS Native Tools x64).
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /utf-8 /Fe:%~dp0wininfo.exe /Fo:%~dp0 %~dp0wininfo.c user32.lib dwmapi.lib shell32.lib ole32.lib
