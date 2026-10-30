<#
.SYNOPSIS
    Publishes a ZENITH LANCER release on GitHub as a DRAFT: the repository (made if missing), master, the tag
    v<version> and a draft release with the two archives from dist, their checksums and the changelog's section.

.DESCRIPTION
    .\Deploy\publish_release.ps1 [-Tag v1.0.0] [-Publish]

    Run after Deploy\build_release.ps1, with everything committed on master. Without -Publish nothing leaves the
    machine and nothing is changed in git: the script runs its checks, writes the release text and the checksums to
    work\publish\<tag> and prints what it would do. With -Publish, and only when every check passes, it does it (it
    needs the GitHub CLI gh, logged in as reneweller-coding):

      1. gh repo create reneweller-coding/ZenithLancer --public, where the repository is missing (and the remote
         origin, where that is missing)
      2. git push -u origin master
      3. git tag -a v<version> (unless it is there), git push origin v<version>
      4. gh release create v<version> --draft --verify-tag with both archives, SHA256SUMS.txt and the text (a draft
         that is already there gets the files and the text instead)

    The checks: the tag names the version in CMakeLists.txt, the branch is master, the working tree is clean, a tag
    that is already there points at HEAD, the remote origin (if there is one) is the GitHub repository, CHANGELOG.md
    has a section for the version, README.md links no other version's downloads, the pictures README.md shows are in
    docs\shots, dist holds both archives.

    The pushed tag starts .github/workflows/release.yml: it builds both archives on GitHub's runners as the check that
    the tag builds and passes its tests, and attaches them only where the draft lacks them, so the archives published
    here and their checksums stay. The draft is made public by hand after a last look (docs\RELEASE.md).

    -Tag       the tag to publish; default v<the version in CMakeLists.txt>
    -Publish   do it; without it the script only checks and prints

.EXAMPLE
    .\Deploy\publish_release.ps1
    .\Deploy\publish_release.ps1 -Publish
#>
param(
    [string]$Tag = "",
    [switch]$Publish
)
$ErrorActionPreference = "Stop"
$Repo = "reneweller-coding/ZenithLancer"
$About = "A vertical scrolling shooter with a workshop between the missions: five episodes, 60 missions, arcade for two, boss rush. Windows, Linux, Steam Deck."
$root = Split-Path $PSScriptRoot -Parent

$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\( ZenithLancer VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { throw "no version in CMakeLists.txt" }
if ($Tag -eq "") { $Tag = "v$version" }
$mode = if ($Publish) { "publish" } else { "dry run: nothing leaves the machine" }
Write-Host "ZENITH LANCER $version, tag $Tag ($mode)"
Write-Host ""

$problems = New-Object System.Collections.Generic.List[string]
function Check([bool]$ok, [string]$good, [string]$bad) {
    if ($ok) {
        Write-Host "  ok    $good"
    } else {
        Write-Host "  FAIL  $bad"
        $problems.Add($bad)
    }
}

# The checks.
Write-Host "Checks"
Check ($Tag -eq "v$version") "the tag $Tag names the version in CMakeLists.txt" "the tag $Tag does not name the version in CMakeLists.txt ($version)"

$branch = (& git -C $root rev-parse --abbrev-ref HEAD) | Select-Object -First 1
Check ($branch -eq "master") "on master" "on the branch ${branch}: publishing pushes master (merge first)"

$dirty = @(& git -C $root status --porcelain)
Check ($dirty.Count -eq 0) "the working tree is clean" "the working tree is not clean ($($dirty.Count) changes): commit first"

$head = (& git -C $root rev-parse HEAD) | Select-Object -First 1
$tagged = (& git -C $root rev-parse -q --verify "refs/tags/$Tag^{commit}") | Select-Object -First 1
$tagExists = [bool]$tagged
if ($tagExists) {
    Check ($tagged -eq $head) "the tag $Tag is there and points at HEAD" "the tag $Tag is there but points at $($tagged.Substring(0, 7)), not at HEAD"
} else {
    Write-Host "  ok    the tag $Tag is not there yet (it will be made at HEAD $($head.Substring(0, 7)))"
}

$remotes = @(& git -C $root remote)
$hasOrigin = $remotes -contains "origin"
if ($hasOrigin) {
    $originUrl = (& git -C $root remote get-url origin) | Select-Object -First 1
    Check ($originUrl -match [regex]::Escape($Repo)) "the remote origin is $originUrl" "the remote origin is $originUrl, not $Repo"
} else {
    Write-Host "  ok    no remote origin yet (it will be https://github.com/$Repo.git)"
}

$changelog = @(Get-Content -Encoding UTF8 (Join-Path $root "CHANGELOG.md"))
$start = -1
for ($i = 0; $i -lt $changelog.Count; ++$i) {
    if ($changelog[$i] -match ('^## \[?' + [regex]::Escape($version) + '\]?(\s|$)')) { $start = $i; break }
}
$section = ""
if ($start -ge 0) {
    $end = $changelog.Count
    for ($i = $start + 1; $i -lt $changelog.Count; ++$i) { if ($changelog[$i] -match '^## ') { $end = $i; break } }
    if ($end -gt $start + 1) { $section = ($changelog[($start + 1)..($end - 1)] -join "`n").Trim() }
}
Check ($section -ne "") "CHANGELOG.md has a section for $version" "CHANGELOG.md has no (or an empty) section '## [$version]'"

$readme = [System.IO.File]::ReadAllText((Join-Path $root "README.md"))
$stale = @([regex]::Matches($readme, 'releases/download/v([0-9.]+)/') | ForEach-Object { $_.Groups[1].Value } |
           Where-Object { $_ -ne $version } | Select-Object -Unique)
Check ($stale.Count -eq 0) "README.md links the downloads of $Tag only" "README.md links the downloads of v$($stale -join ', v')"

$shots = @([regex]::Matches($readme, 'docs/shots/[A-Za-z0-9_.-]+\.png') | ForEach-Object { $_.Value } | Select-Object -Unique)
$missingShots = @($shots | Where-Object { -not (Test-Path (Join-Path $root $_)) })
Check ($missingShots.Count -eq 0) "the $($shots.Count) pictures README.md shows are there" "$($missingShots.Count) of the $($shots.Count) pictures README.md shows are missing in docs\shots: $(($missingShots | ForEach-Object { Split-Path -Leaf $_ }) -join ', ')"

$archives = @(
    @{ File = "ZenithLancer-$version-windows-x64.zip";
       Says = "Windows 10 and 11 (x64): unpack it anywhere and start ZenithLancer.exe, nothing to install" },
    @{ File = "ZenithLancer-$version-linux-x86_64.tar.gz";
       Says = "Linux (x86-64, glibc 2.35 or newer) and the Steam Deck: unpack it and start ./ZenithLancer; on the Deck add it to Steam as a non-Steam game" }
)
$files = @()
foreach ($a in $archives) {
    $path = Join-Path $root "dist\$($a.File)"
    if (Test-Path $path) {
        $item = Get-Item $path
        Check $true ("dist\{0} ({1:N0} MB, {2:dd.MM.yyyy HH:mm})" -f $a.File, ($item.Length / 1MB), $item.LastWriteTime) ""
        $files += $path
    } else {
        Check $false "" "dist\$($a.File) is missing: .\Deploy\build_release.ps1"
    }
}

# The release text and the checksums (in work, which git ignores).
$pub = Join-Path $root "work\publish\$Tag"
New-Item -ItemType Directory -Force $pub | Out-Null
$sums = Join-Path $pub "SHA256SUMS.txt"
if (Test-Path $sums) { Remove-Item -Force $sums }
$files | ForEach-Object { "{0}  {1}" -f (Get-FileHash -Algorithm SHA256 $_).Hash.ToLower(), (Split-Path -Leaf $_) } |
    Set-Content -Encoding ascii $sums
$lines = @($archives | ForEach-Object { "- **{0}** -- {1}." -f $_.File, $_.Says })
$lines += "- **SHA256SUMS.txt** -- the checksums of both."
$footer = @'
Requirements: a graphics card with OpenGL 3.3; a gamepad, the keyboard or the mouse. The program is not code-signed:
Windows' SmartScreen may warn once ("More info", "Run anyway"). Saves and settings live in %APPDATA%\ZenithLancer
(Windows) and ~/.local/share/ZenithLancer (Linux, Steam Deck). Licence: AGPL-3.0; the source is this repository at
the tag, and CREDITS.txt in the archives names the libraries, the font and their licences.
'@
$text = $section + "`n`n## Downloads`n`n" + ($lines -join "`n") + "`n`n" + $footer.Trim() + "`n"
$textFile = Join-Path $pub "release.md"
[System.IO.File]::WriteAllText($textFile, $text, (New-Object System.Text.UTF8Encoding($false)))
Write-Host ""
Write-Host "Release text and checksums: $pub"

# What it does (or would do).
$gh = Get-Command gh -ErrorAction SilentlyContinue
$title = "Zenith Lancer $version"
$upload = @($files) + @($sums)
$uploadNames = (@($archives | ForEach-Object { $_.File }) + @("SHA256SUMS.txt")) -join " "
Write-Host ""
Write-Host "Steps"
Write-Host "  1. gh repo create $Repo --public --description `"...`"$(if (-not $hasOrigin) { ' --source . --remote origin' }) (only where the repository is missing)"
if ($hasOrigin) { Write-Host "     (the remote origin is there)" }
Write-Host "  2. git push -u origin master"
if ($tagExists) {
    Write-Host "  3. git push origin $Tag (the tag is there)"
} else {
    Write-Host "  3. git tag -a $Tag -m `"$title`"; git push origin $Tag"
}
Write-Host "  4. gh release create $Tag $uploadNames --repo $Repo --draft --verify-tag --title `"$title`" --notes-file release.md"
Write-Host "     (where the draft is there already: gh release upload --clobber and gh release edit --notes-file)"
Write-Host "  The tag starts .github/workflows/release.yml; the draft is made public by hand after a last look."
if (-not $gh) { Write-Host "  note: gh (the GitHub CLI) is not installed; -Publish needs it" }
Write-Host ""

if ($problems.Count -gt 0) {
    Write-Host "Not ready: $($problems.Count) check(s) failed."
    if ($Publish) { throw "nothing published: $($problems.Count) check(s) failed" }
    return
}
if (-not $Publish) {
    Write-Host "Ready. Dry run: nothing pushed, nothing published. Publish with -Publish."
    return
}
if (-not $gh) { throw "gh (the GitHub CLI) is missing: https://cli.github.com" }

# 1. The repository and the remote.
& gh repo view $Repo --json name | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "making the repository $Repo"
    if ($hasOrigin) {
        & gh repo create $Repo --public --description $About
    } else {
        & gh repo create $Repo --public --description $About --source $root --remote origin
    }
    if ($LASTEXITCODE -ne 0) { throw "gh repo create failed" }
} elseif (-not $hasOrigin) {
    & git -C $root remote add origin "https://github.com/$Repo.git"
    if ($LASTEXITCODE -ne 0) { throw "git remote add failed" }
}

# 2. master.
& git -C $root push -u origin master
if ($LASTEXITCODE -ne 0) { throw "git push failed" }

# 3. The tag (first: --verify-tag wants it on GitHub; the workflow it starts builds for some minutes, then finds the
#    draft with its archives and keeps them).
if (-not $tagExists) {
    & git -C $root tag -a $Tag -m $title
    if ($LASTEXITCODE -ne 0) { throw "git tag failed" }
}
& git -C $root push origin $Tag
if ($LASTEXITCODE -ne 0) { throw "pushing the tag failed" }

# 4. The draft release.
& gh release view $Tag --repo $Repo --json isDraft | Out-Null
if ($LASTEXITCODE -eq 0) {
    & gh release upload $Tag @upload --repo $Repo --clobber
    if ($LASTEXITCODE -ne 0) { throw "gh release upload failed" }
    & gh release edit $Tag --repo $Repo --title $title --notes-file $textFile
    if ($LASTEXITCODE -ne 0) { throw "gh release edit failed" }
} else {
    & gh release create $Tag @upload --repo $Repo --draft --verify-tag --title $title --notes-file $textFile
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
}
Write-Host ""
Write-Host "Draft release ${Tag}: https://github.com/$Repo/releases"
Write-Host "After a last look: gh release edit $Tag --repo $Repo --draft=false"
