@echo off
rem Run hooklist: without arguments - interactive option menu;
rem with arguments - passes them to the exe as is (example: run-hooklist.cmd -shell -enum)
rem Menu prompts are ASCII on purpose: non-ASCII in executable lines breaks cmd parsing.
setlocal
if not "%~1"=="" goto run

echo === hooklist: launch options (Enter = default) ===
set "ANS="
set "MODE=-winevent"
set /p "ANS=Hook mode: W=winevent (Enter) / S=shell: "
if /I "%ANS%"=="S" set "MODE=-shell"
set "ANS="
set "ENUM=-noenum"
set /p "ANS=Initial EnumWindows: Y=yes / N=no (Enter): "
if /I "%ANS%"=="Y" set "ENUM=-enum"
set "LOG=hooklist.log"
set /p "LOG=Log file [hooklist.log]: "
set "ARGS=%MODE% %ENUM% "%LOG%""

:run
if not exist "%~dp0hooklist.exe" call "%~dp0build-hooklist.bat"
"%~dp0hooklist.exe" %ARGS% %*
endlocal
pause
