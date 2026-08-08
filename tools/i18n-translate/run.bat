@echo off
chcp 65001 >nul
title sphaira i18n translate
cd /d "%~dp0"

if not exist .venv (
    echo Creating venv...
    python -m venv .venv || goto :fail
    .venv\Scripts\python.exe -m pip install -q --upgrade pip requests || goto :fail
)

set PYTHONIOENCODING=utf-8
set PYTHONUTF8=1

echo Checking proxy at http://127.0.0.1:8081 ...
.venv\Scripts\python.exe -c "import requests;requests.get('http://127.0.0.1:8081/v1/models',timeout=5)" 2>nul
if errorlevel 1 (
    echo.
    echo Gemini Web2API is not running. Start D:\git\dev\gemini-web2api\run.bat first.
    echo.
    goto :fail
)

.venv\Scripts\python.exe translate.py %*
goto :end

:fail
echo.
echo FAILED.

:end
echo.
pause
