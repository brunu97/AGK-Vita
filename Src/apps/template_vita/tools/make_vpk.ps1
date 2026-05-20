# AGK Vita Tier 2 - build the engine + game and pack the VPK.
#
#   1. CMake builds the .self (engine + game C++).
#   2. Everything is staged into build/_pack/ and vita-pack-vpk is invoked
#      from there with relative paths only, so absolute paths containing
#      spaces never reach the tool.

$ErrorActionPreference = "Stop"

# Project root = parent of tools/
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

Write-Host "=== AGK Vita Tier 2 - build VPK ===" -ForegroundColor Cyan

# ---- 0. Sanity check: are we inside an AGK engine clone? ----
# The CMakeLists references the engine via ../.. (the AGK/ root). If it's
# missing, we're being run from the agk-vita-port/Src/ distribution copy,
# which is not buildable on its own.
if ( -not (Test-Path "../../common/Source/Wrapper.cpp") ) {
    Write-Host ""
    Write-Host "ERROR: AGK engine source not found." -ForegroundColor Red
    Write-Host "This folder is a distribution copy of the Vita port, not a buildable tree." -ForegroundColor Red
    Write-Host ""
    Write-Host "To build, deploy the port onto an AGK engine clone first:" -ForegroundColor Yellow
    Write-Host "  1. git clone https://github.com/TheGameCreators/AGKRepo" -ForegroundColor Yellow
    Write-Host "  2. Copy  Src/platform-vita/    into  AGKRepo/AGK/platform/vita/" -ForegroundColor Yellow
    Write-Host "  3. Copy  Src/apps/template_vita/  into  AGKRepo/AGK/apps/template_vita/" -ForegroundColor Yellow
    Write-Host "  4. Apply Src/patches/engine.patch" -ForegroundColor Yellow
    Write-Host "  5. Run MAKE_VPK.bat from AGKRepo/AGK/apps/template_vita/" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "See TIER2_GUIDE.md for the full workflow." -ForegroundColor Yellow
    exit 1
}

# ---- 1. Locate the vitasdk ----
if ( -not $env:VITASDK ) {
    if ( Test-Path "C:\vitasdk\bin\arm-vita-eabi-gcc.exe" ) {
        $env:VITASDK = "C:\vitasdk"
    } else {
        Write-Host "ERROR: VITASDK is not set and C:\vitasdk does not exist." -ForegroundColor Red
        Write-Host "Install the vitasdk and either set VITASDK or place it at C:\vitasdk." -ForegroundColor Red
        exit 1
    }
}
$env:PATH = "$env:VITASDK\bin;" + $env:PATH

# ---- 2. Locate CMake ----
$cmake = "cmake"
if ( -not (Get-Command cmake -ErrorAction SilentlyContinue) ) {
    $cmake = "$env:USERPROFILE\scoop\apps\cmake\current\bin\cmake.exe"
    if ( -not (Test-Path $cmake) ) {
        Write-Host "ERROR: CMake not found on PATH or under scoop." -ForegroundColor Red
        exit 1
    }
}

# ---- 3. Read config.txt ----
$GameName = "AGK Vita Game"
$TitleId  = "AGKG00001"
$AppVer   = "01.00"
if ( Test-Path "config.txt" ) {
    foreach ($line in Get-Content "config.txt") {
        $t = $line.Trim()
        if ($t -eq "" -or $t.StartsWith("#") -or $t.StartsWith(";")) { continue }
        $kv = $t -split "=", 2
        if ($kv.Count -ne 2) { continue }
        $k = $kv[0].Trim(); $v = $kv[1].Trim()
        if ($k -eq "GAME_NAME")   { $GameName = $v }
        if ($k -eq "TITLE_ID")    { $TitleId  = $v }
        if ($k -eq "APP_VERSION") { $AppVer   = $v }
    }
}

if ($TitleId -notmatch '^[A-Z]{4}[0-9]{5}$') {
    Write-Host "ERROR: TITLE_ID '$TitleId' must be 4 uppercase letters + 5 digits (e.g. AGKG00001)." -ForegroundColor Red
    exit 1
}

Write-Host "Game name : $GameName"
Write-Host "Title ID  : $TitleId"
Write-Host "Version   : $AppVer"
Write-Host ""

# ---- 4. CMake configure (once) + build the .self ----
if ( -not (Test-Path "build/CMakeCache.txt") ) {
    Write-Host "Configuring CMake (first build only)..."
    & $cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=mingw32-make
    if ($LASTEXITCODE -ne 0) { Write-Host "CMake configure failed." -ForegroundColor Red; exit 1 }
    Write-Host ""
}

Write-Host "Building..."
& $cmake --build build
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed." -ForegroundColor Red; exit 1 }

$selfFile = "build/agk_template_vita.self"
if ( -not (Test-Path $selfFile) ) {
    Write-Host "ERROR: $selfFile was not produced." -ForegroundColor Red
    exit 1
}

# ---- 5. Stage everything for vita-pack-vpk ----
$Stage = "build/_pack"
if ( Test-Path $Stage ) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Path $Stage | Out-Null

Copy-Item $selfFile "$Stage/eboot.bin"

# media/ - mirror contents at the VPK root (subfolders preserved)
if ( Test-Path "media" ) {
    Get-ChildItem -Path "media" -Force | ForEach-Object {
        Copy-Item $_.FullName "$Stage/$($_.Name)" -Recurse -Force
    }
}

# sce_sys (icon + livearea); param.sfo joins it next
Copy-Item "sce_sys" "$Stage/" -Recurse -Force

# ---- 6. param.sfo ----
Write-Host "Generating param.sfo..."
& "$env:VITASDK\bin\vita-mksfoex.exe" -s "TITLE_ID=$TitleId" -s "APP_VER=$AppVer" $GameName "$Stage/sce_sys/param.sfo"
if ($LASTEXITCODE -ne 0) { Write-Host "vita-mksfoex failed." -ForegroundColor Red; exit 1 }

# ---- 7. vita-pack-vpk - invoked from the staging dir, relative paths ----
$VpkName = ($GameName -replace '[^\w\-]', '_') + ".vpk"

# NOTE: do not name this $args - that is a reserved automatic variable.
# Pass only TOP-LEVEL entries; vita-pack-vpk recurses into directories itself.
# Passing one -a per file overflows the Windows command-line limit (~32 KB)
# once a game has many assets.
$packArgs = @("-s", "sce_sys/param.sfo", "-b", "eboot.bin")
Get-ChildItem -Path $Stage | ForEach-Object {
    $name = $_.Name
    if ($name -eq "eboot.bin") { return }
    $packArgs += "-a"
    $packArgs += "$name=$name"
}
$packArgs += "..\..\$VpkName"

Write-Host "Packing $VpkName..."
Push-Location $Stage
& "$env:VITASDK\bin\vita-pack-vpk.exe" @packArgs
$packExit = $LASTEXITCODE
Pop-Location

if ($packExit -ne 0) {
    Write-Host "vita-pack-vpk failed." -ForegroundColor Red
    exit 1
}

# Cleanup staging
Remove-Item -Recurse -Force $Stage

# ---- 8. Done ----
$vpkPath = Join-Path $Root $VpkName
$size = [math]::Round((Get-Item $vpkPath).Length / 1KB, 1)
Write-Host ""
Write-Host "=== DONE ===" -ForegroundColor Green
Write-Host "VPK: $vpkPath  ($size KB)" -ForegroundColor Green
Write-Host "Install it on the Vita with VitaShell."
