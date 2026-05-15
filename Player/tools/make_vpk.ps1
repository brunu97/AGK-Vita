# AGKVitaPlayer - VPK builder
#
# Bundles the prebuilt AGK interpreter (bin/eboot.bin) together with the
# game files in game/ into an installable Vita .vpk.
#
# Called by MAKE_VPK.bat. Pure PowerShell — no Python, no extra installs.

$ErrorActionPreference = "Stop"

# Root = parent of this tools/ folder.
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

Write-Host "=== AGKVitaPlayer - building VPK ===" -ForegroundColor Cyan

# ---- 1. Read config.txt --------------------------------------------------
$GameName = "AGK Game"
$TitleId  = "AGKG00001"
foreach ($line in Get-Content "config.txt") {
    $t = $line.Trim()
    if ($t -eq "" -or $t.StartsWith(";")) { continue }
    $kv = $t -split "=", 2
    if ($kv.Count -ne 2) { continue }
    $key = $kv[0].Trim(); $val = $kv[1].Trim()
    if ($key -eq "GAME_NAME") { $GameName = $val }
    if ($key -eq "TITLE_ID")  { $TitleId  = $val }
}

if ($TitleId -notmatch '^[A-Z]{4}[0-9]{5}$') {
    Write-Host "ERROR: TITLE_ID '$TitleId' is invalid." -ForegroundColor Red
    Write-Host "It must be exactly 4 uppercase letters + 5 digits, e.g. AGKG00001." -ForegroundColor Red
    exit 1
}

Write-Host "Game name : $GameName"
Write-Host "Title ID  : $TitleId"

# ---- 2. Check the game folder has bytecode -------------------------------
if (-not (Test-Path "game\bytecode.byc")) {
    Write-Host ""
    Write-Host "ERROR: game\bytecode.byc not found." -ForegroundColor Red
    Write-Host "Build your project in the AGK IDE, then copy the contents of" -ForegroundColor Red
    Write-Host "the project's 'media' folder (which includes bytecode.byc and" -ForegroundColor Red
    Write-Host "all assets) into the game\ folder here." -ForegroundColor Red
    exit 1
}

# ---- 3. Generate param.sfo ----------------------------------------------
Write-Host ""
Write-Host "Generating param.sfo..."
& "bin\vita-mksfoex.exe" -s "TITLE_ID=$TitleId" -s "APP_VER=01.00" $GameName "param.sfo"
if ($LASTEXITCODE -ne 0) { Write-Host "vita-mksfoex failed" -ForegroundColor Red; exit 1 }

# ---- 4. Stage everything into a clean _build folder ----------------------
# vita-pack-vpk mishandles spaces in paths, so we stage under a relative
# path and invoke it from there.
$Stage = "_build"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Path $Stage | Out-Null
New-Item -ItemType Directory -Path "$Stage\sce_sys\livearea\contents" | Out-Null

Copy-Item "bin\eboot.bin"  "$Stage\eboot.bin"
Copy-Item "param.sfo"      "$Stage\param.sfo"
Copy-Item "sce_sys\icon0.png"                       "$Stage\sce_sys\icon0.png"
Copy-Item "sce_sys\livearea\contents\bg.png"        "$Stage\sce_sys\livearea\contents\bg.png"
Copy-Item "sce_sys\livearea\contents\startup.png"   "$Stage\sce_sys\livearea\contents\startup.png"
Copy-Item "sce_sys\livearea\contents\template.xml"  "$Stage\sce_sys\livearea\contents\template.xml"

# Mirror the whole game/ folder into the VPK root (app0:/). This carries
# bytecode.byc plus any media subfolders/assets the game references.
Copy-Item "game\*" "$Stage\" -Recurse -Force

# ---- 5. Build the vita-pack-vpk argument list ----------------------------
# NOTE: do not name this $args — that is a reserved automatic variable.
# Add only TOP-LEVEL entries: vita-pack-vpk recurses into directories itself.
# Passing one -a per file overflows the Windows command-line limit (~32 KB)
# once a game has many assets.
$packArgs = @("-s", "param.sfo", "-b", "eboot.bin")
Get-ChildItem -Path $Stage | ForEach-Object {
    $name = $_.Name
    if ($name -eq "eboot.bin" -or $name -eq "param.sfo") { return }
    $packArgs += "-a"
    $packArgs += "$name=$name"
}

$VpkName = ($GameName -replace '[^\w\-]', '_') + ".vpk"
$packArgs += "..\$VpkName"

# ---- 6. Pack -------------------------------------------------------------
Write-Host "Packing $VpkName..."
Push-Location $Stage
& "..\bin\vita-pack-vpk.exe" @packArgs
$packExit = $LASTEXITCODE
Pop-Location

if ($packExit -ne 0) {
    Write-Host "vita-pack-vpk failed" -ForegroundColor Red
    exit 1
}

# ---- 7. Cleanup ----------------------------------------------------------
Remove-Item -Recurse -Force $Stage
Remove-Item -Force "param.sfo"

$vpkPath = Join-Path $Root $VpkName
$size = [math]::Round((Get-Item $vpkPath).Length / 1KB, 1)
Write-Host ""
Write-Host "=== DONE ===" -ForegroundColor Green
Write-Host "VPK: $vpkPath  ($size KB)" -ForegroundColor Green
Write-Host "Install it on your Vita with VitaShell."
