@echo off
rem FETCH_UPSTREAM.cmd - download the pinned TFE upstream with curl.exe
rem (no git CLI, no PowerShell). ASCII only.
set PIN=ed9e51c315078d6460551593e48fd294e131fc83
if exist upstream\src\TheForceEngine\TFE_DarkForces\darkForcesMain.cpp (
  echo upstream already present, pin %PIN%
  goto :eof
)
mkdir upstream 2>nul
echo downloading TFE pin %PIN% ...
curl.exe -L -o upstream\tfe_pin.zip "https://codeload.github.com/TheForceEngine/TheForceEngine/zip/%PIN%"
if errorlevel 1 ( echo ERROR: download failed & exit /b 1 )
cd upstream
tar -xf tfe_pin.zip
if errorlevel 1 ( echo ERROR: extract failed & exit /b 1 )
if exist src rmdir /s /q src

rename TheForceEngine-%PIN% src

del tfe_pin.zip
cd ..
echo NOTE: engine root is upstream\src\TheForceEngine
echo done. build with BUILD_NEXT_WINDOWS.cmd
