@echo off
chcp 65001 >nul
title sphaira i18n translate - tests
cd /d "%~dp0"

if not exist .venv (
    echo Creating venv...
    python -m venv .venv || goto :fail
    .venv\Scripts\python.exe -m pip install -q --upgrade pip requests || goto :fail
)

set PYTHONIOENCODING=utf-8
set PYTHONUTF8=1

rem no args: offline only. pass --live to also send real requests to the proxy.
.venv\Scripts\python.exe test_translate.py %*
goto :end

:fail
echo.
echo FAILED.

:end
echo.
pause
