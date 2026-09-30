@echo off
setlocal EnableExtensions
rem ============================================================
rem  ChromaX setup - free, no administrator rights required
rem
rem  Copies ChromaX.exe to %LOCALAPPDATA%\Programs\ChromaX and
rem  creates a Start Menu shortcut.  Optional: desktop shortcut
rem  and "start with Windows".  Settings always live in
rem  %APPDATA%\ChromaX (shared with the portable EXE).
rem
rem  Run it from a folder that also contains ChromaX.exe.
rem ============================================================

set "SRC=%~dp0ChromaX.exe"
if not exist "%SRC%" (
    echo Error: ChromaX.exe must sit next to ChromaX-setup.bat
    exit /b 1
)

set "DEST=%LOCALAPPDATA%\Programs\ChromaX"
set "MENULNK=%APPDATA%\Microsoft\Windows\Start Menu\Programs\ChromaX.lnk"
set "DESKLNK=%USERPROFILE%\Desktop\ChromaX.lnk"
set "RUNKEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Run"

echo Installing ChromaX to %DEST% ...
if not exist "%DEST%" mkdir "%DEST%"
copy /y "%SRC%" "%DEST%\" >nul
if errorlevel 1 (
    echo Error: could not copy ChromaX.exe to %DEST%
    exit /b 1
)

echo Creating Start Menu shortcut ...
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%MENULNK%');$s.TargetPath='%DEST%\ChromaX.exe';$s.WorkingDirectory='%DEST%';$s.Description='ChromaX - free Windows display and gaming visual control center';$s.Save()"
if errorlevel 1 echo Warning: Start Menu shortcut could not be created.

set /p DESKTOP="Create a desktop shortcut? [Y/n] "
if /i not "%DESKTOP%"=="n" (
    powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%DESKLNK%');$s.TargetPath='%DEST%\ChromaX.exe';$s.WorkingDirectory='%DEST%';$s.Save()"
)

set /p STARTWIN="Start ChromaX with Windows? [y/N] "
if /i "%STARTWIN%"=="y" (
    reg add "%RUNKEY%" /v ChromaX /t REG_SZ /d "\"%DEST%\ChromaX.exe\"" /f >nul
    if errorlevel 1 echo Warning: start-up entry could not be created.
)

echo.
echo Done.
echo   Installed to : %DEST%
echo   Settings in  : %APPDATA%\ChromaX
echo   Uninstall    : ChromaX-uninstall.bat
endlocal
exit /b 0
