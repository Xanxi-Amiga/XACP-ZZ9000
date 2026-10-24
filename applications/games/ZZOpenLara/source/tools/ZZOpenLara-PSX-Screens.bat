@echo off
rem ZZOpenLara - PSX screens conversion: standalone EXE preferred; Python fallback.
setlocal
pushd "%~dp0"
if exist "%~dp0ZZOpenLara-PSX-Screens.exe" (
  "%~dp0ZZOpenLara-PSX-Screens.exe" %*
  set "RET=%ERRORLEVEL%"
  goto :done
)
set "PY="
where py >nul 2>nul && set "PY=py -3"
if not defined PY (where python >nul 2>nul && set "PY=python")
if not defined PY (
  echo ZZOpenLara-PSX-Screens.exe is missing. This fallback requires Python 3.
  echo ZZOpenLara-PSX-Screens.exe absent. Cette solution de secours necessite Python 3.
  set "RET=1"
  goto :done
)
%PY% "%~dp0ol_psx_screens.py" %*
set "RET=%ERRORLEVEL%"
:done
echo.
if not "%RET%"=="0" echo Conversion failed / Echec de conversion : %RET%
pause
popd
exit /b %RET%
