@echo off
title DFEE Workspace Launcher
echo ==============================================================
echo     Starting Deterministic Film Emulation Engine (DFEE)
echo ==============================================================
echo.
echo Launching local server on http://127.0.0.1:8000 ...

:: Launch default browser in the background after a brief 2 second delay
start /b cmd /c "timeout /t 2 >nul && start http://127.0.0.1:8000"

:: Start the Python FastAPI server
python server.py
pause
