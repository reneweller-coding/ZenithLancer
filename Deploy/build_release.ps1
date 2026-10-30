<#
.SYNOPSIS
    Packs ZENITH LANCER for handing out: dist\ZenithLancer-<version>-windows-x64.zip and, through WSL, the Linux
    archive (for the Steam Deck as well).

.DESCRIPTION
    .\Deploy\build_release.ps1 [-NoBuild] [-NoLinux]

    Builds the Release (build.ps1), runs the headless tests (ctest -R "data|missions": every data file, every mission
    flown by the autopilot), checks the data (--validate), stages the game in work\release and zips it into dist:
    the program, data\, assets\ (fonts, music, portraits, voices: all of it), the player's readme as README.txt,
    CREDITS.txt, LICENSE and, once docs\example_mod exists, the example mod as mods\example. The staged program then
    checks the data beside it, so the archive holds what the player needs. The Linux archive comes from
    Deploy/build_linux.sh in WSL (GCC 12, glibc 2.35 and newer, the Steam Deck included); without WSL it is skipped.
    .github/workflows/release.yml runs this script with -NoLinux on GitHub's Windows runner.

    -NoBuild   pack what bin\msvc holds (no build, no tests)
    -NoLinux   only the Windows archive

.EXAMPLE
    .\Deploy\build_release.ps1
#>
param(
    [switch]$NoBuild,
    [switch]$NoLinux
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\( ZenithLancer VERSION ([0-9.]+)').Matches[0].Groups[1].Value
Write-Host "ZENITH LANCER $version"

if (-not $NoBuild) {
    & (Join-Path $root "build.ps1")
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
    # The headless tests only: the app tests play the screens in a window (build.ps1 -Test runs them all).
    Push-Location $root
    try {
        & ctest --preset msvc -R "data|missions"
        if ($LASTEXITCODE -ne 0) { throw "the tests failed" }
    } finally {
        Pop-Location
    }
}
$exe = Join-Path $root "bin\msvc\ZenithLancer.exe"
& $exe --validate --data (Join-Path $root "data") | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { throw "the data has errors" }

$name = "ZenithLancer-$version-windows-x64"
$stage = Join-Path $root "work\release\$name"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
# Whole folders: what assets\ gains (portraits, voices) goes along. The example mod only once it exists.
$folders = @(@("data", "data"), @("assets", "assets"), @("docs\example_mod", "mods\example"))
foreach ($f in $folders) {
    $from = Join-Path $root $f[0]
    if (-not (Test-Path $from)) {
        Write-Host "not there, skipped: $($f[0])"
        continue
    }
    & robocopy $from (Join-Path $stage $f[1]) /E /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "copying $($f[0]) failed" }
}
Copy-Item (Join-Path $root "docs\README-player.txt") (Join-Path $stage "README.txt")
Copy-Item (Join-Path $root "docs\CREDITS.txt") $stage
Copy-Item (Join-Path $root "LICENSE") $stage
# The packed program checks the data beside it (no --data): what the archive holds is what the player gets.
& (Join-Path $stage "ZenithLancer.exe") --validate | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { throw "the staged game does not find its data" }

$dist = Join-Path $root "dist"
New-Item -ItemType Directory -Force $dist | Out-Null
$zip = Join-Path $dist "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
# Windows' own tar (bsdtar) zips faster than Compress-Archive.
& (Join-Path $env:SystemRoot "System32\tar.exe") -a -c -f $zip -C (Join-Path $root "work\release") $name
if ($LASTEXITCODE -ne 0) { throw "zip failed" }
Write-Host ("{0}  {1:N0} MB" -f $zip, ((Get-Item $zip).Length / 1MB))

if (-not $NoLinux) {
    if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
        Write-Host "no WSL: no Linux archive"
    } else {
        $drive = $root.Substring(0, 1).ToLower()
        $wslRoot = "/mnt/$drive" + ($root.Substring(2) -replace '\\', '/')
        & wsl.exe -d Ubuntu-22.04 -- bash "$wslRoot/Deploy/build_linux.sh"
        if ($LASTEXITCODE -ne 0) { throw "Linux build failed" }
        $tgz = Join-Path $dist "ZenithLancer-$version-linux-x86_64.tar.gz"
        Write-Host ("{0}  {1:N0} MB" -f $tgz, ((Get-Item $tgz).Length / 1MB))
    }
}
