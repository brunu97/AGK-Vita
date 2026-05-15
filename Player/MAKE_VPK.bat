@echo off
REM ===========================================================================
REM AGKVitaPlayer - one-click VPK builder
REM
REM 1. Build your game in the AGK IDE.
REM 2. Copy the contents of your project's "media" folder (it contains
REM    bytecode.byc and all your assets) into the game\ folder here.
REM 3. Edit config.txt - set GAME_NAME and a unique TITLE_ID.
REM 4. Double-click this file.
REM
REM Out comes <GameName>.vpk - install it on the Vita with VitaShell.
REM ===========================================================================

cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\make_vpk.ps1"

echo.
pause
