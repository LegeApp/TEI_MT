@echo off
setlocal
cd /d "%~dp0"

set "PORT=%~1"
if "%PORT%"=="" set "PORT=8765"

echo Serving TEI viewer at: http://127.0.0.1:%PORT%/
echo Press Ctrl+C to stop.
echo.

py -3 -m http.server %PORT%
if errorlevel 1 python -m http.server %PORT%
if errorlevel 1 python3 -m http.server %PORT%
if errorlevel 1 (
  echo.
  echo ERROR: Could not start Python's http.server.
  echo Install Python 3 from https://www.python.org/downloads/ ^(check "Add to PATH"^)
  echo or run:  run_viewer.ps1
  exit /b 1
)
