@echo off
echo.
echo.
title Restore MPC Script Source default settings...
start /min reg delete "HKEY_CURRENT_USER\Software\MPC-BE Filters\MPC Script Source" /f
echo    settings were reset to default
echo.
pause >NUL
