@echo off
setlocal EnableExtensions
rem ============================================================
rem  ChromaX uninstall
rem
rem  Removes the installed copy, the shortcuts and the optional
rem  "start with Windows" entry.  Your settings in
rem  %APPDATA%\ChromaX are kept - delete that folder manually
rem  to wipe them.
rem ============================================================

set "DEST=%LOCALAPPDATA%\Programs\ChromaX"
set "MENULNK=%APPDATA%\Microsoft\Windows\Start Menu\Programs\ChromaX.lnk"
set "DESKLNK=%USERPROFILE%\Desktop\ChromaX.lnk"

taskkill /f /im ChromaX.exe >nul 2>nul
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v ChromaX /f >nul 2>nul
del "%MENULNK%" >nul 2>nul
del "%DESKLNK%" >nul 2>nul
rd /s /q "%DEST%" >nul 2>nul

echo Removed the installed copy, shortcuts and start-up entry.
echo Settings kept in %APPDATA%\ChromaX (delete that folder to wipe them).
endlocal
exit /b 0
