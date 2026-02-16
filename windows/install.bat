@echo off
:: Quick launcher - right-click > Run as Administrator is NOT needed.
:: Just double-click or run from any terminal.
echo.
echo   Ciao Prolog - Windows Installer
echo   ================================
echo.
echo   Running installer...
echo.
powershell -ExecutionPolicy Bypass -File "%~dp0install.ps1"
echo.
pause
