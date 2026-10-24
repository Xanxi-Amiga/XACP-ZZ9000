@echo off
rem Publisher PC only. Offline. Requires native Windows Python and PyInstaller already installed.
setlocal
pushd "%~dp0"
python -c "import sys; assert sys.platform == 'win32'" >nul 2>nul
if errorlevel 1 (
 echo ERROR: native Windows Python not found.
 popd
 exit /b 1
)
python -m PyInstaller --version >nul 2>nul
if errorlevel 1 (
 echo ERROR: PyInstaller is not installed.
 popd
 exit /b 1
)
python -m PyInstaller --onefile --clean --noconfirm --name ZZOpenLara-Music --distpath . --workpath _WIN_build\music --specpath _WIN_build ol_music.py
if errorlevel 1 goto :failed
python -m PyInstaller --onefile --clean --noconfirm --name ZZOpenLara-PSX-Screens --distpath . --workpath _WIN_build\screens --specpath _WIN_build ol_psx_screens.py
if errorlevel 1 goto :failed
echo EXEs ready.
popd
exit /b 0
:failed
echo ERROR: Windows EXE build failed.
popd
exit /b 1
