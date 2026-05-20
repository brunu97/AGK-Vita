@echo off
REM ===========================================================================
REM  AGK Vita Tier 2 — one-click build + VPK packaging.
REM
REM  Workflow:
REM    1. Drop your game's .cpp / .h into game/.
REM    2. Drop assets the game loads into media/.
REM    3. Edit config.txt (GAME_NAME + TITLE_ID).
REM    4. Provide 8-bit paletted PNGs in sce_sys/ (icon0 + livearea).
REM    5. Double-click this file.
REM
REM  Output: <GameName>.vpk in this folder. Install it with VitaShell.
REM ===========================================================================

cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\make_vpk.ps1"

echo.
pause
