@echo off
cd /d "%~dp0"

if exist "dist\BiliAssetBox\BiliAssetBox.exe" (
  start "" "dist\BiliAssetBox\BiliAssetBox.exe" %*
  exit /b 0
)

if not exist ".venv\Scripts\python.exe" (
  echo [x] .venv not found. Please follow README and create it first.
  pause
  exit /b 1
)

if not exist "build\bin\bili_core.dll" (
  echo [x] bili_core.dll not found. Build the core first:
  echo     .venv\Scripts\python.exe scripts\build_core.py
  pause
  exit /b 1
)

echo Starting BiliAssetBox from source ...
start "" ".venv\Scripts\pythonw.exe" run.py %*
exit /b 0
