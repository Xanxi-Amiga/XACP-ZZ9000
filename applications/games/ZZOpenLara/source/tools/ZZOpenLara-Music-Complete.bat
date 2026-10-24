@echo off
rem ZZOpenLara - choose PSX then PC CUE using Windows file selection dialogs.
rem Windows PowerShell is included with Windows; users do NOT need Python.
setlocal EnableExtensions
if not exist "%~dp0ZZOpenLara-Music.exe" (
  echo ERROR: ZZOpenLara-Music.exe is missing from this folder.
  pause
  exit /b 1
)
if not exist "%~dp0ZZOpenLara-Music-Wizard.ps1" (
  echo ERROR: ZZOpenLara-Music-Wizard.ps1 is missing.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0ZZOpenLara-Music-Wizard.ps1"
set "RESULT=%ERRORLEVEL%"
echo.
if "%RESULT%"=="0" (
  echo 58-track soundtrack ready: tools\complete_music\ol_music.pak
) else (
  echo Conversion cancelled or failed. Existing music files were preserved.
)
echo.
pause
exit /b %RESULT%
