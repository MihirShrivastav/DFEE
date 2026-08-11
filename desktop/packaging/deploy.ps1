# Builds Film Lab (Release) and stages a clean, self-contained deploy folder at
# desktop\out\dist\FilmLab, then zips it to desktop\out\dist\FilmLab-portable.zip.
#
# Run from anywhere:  powershell -ExecutionPolicy Bypass -File desktop\packaging\deploy.ps1
# Then (optional installer):  ISCC.exe desktop\packaging\FilmLab.iss
#
# Prereqs: the Qt MSVC kit (for windeployqt) and a configured CMake build at desktop\out\build.

param(
    [string]$Qt = "C:\Qt\6.8.3\msvc2022_64",
    [string]$Config = "Release"
)
$ErrorActionPreference = "Stop"

$repo  = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $repo "desktop\out\build"
$dist  = Join-Path $repo "desktop\out\dist\FilmLab"
$exe   = Join-Path $dist "FilmLab.exe"

Write-Host "Building DFEE ($Config)..."
cmake --build $build --config $Config --target DFEE

Write-Host "Staging clean deploy folder: $dist"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $dist | Out-Null

Copy-Item (Join-Path $build "$Config\DFEE.exe") $exe

Write-Host "Running windeployqt (Qt runtime + QML)..."
& (Join-Path $Qt "bin\windeployqt.exe") --release --qmldir (Join-Path $repo "desktop\qml") --no-translations $exe | Out-Null

Write-Host "Copying engine (non-Qt) DLLs..."
Copy-Item (Join-Path $build "$Config\*.dll") $dist -Force

Write-Host "Bundling film stock profiles..."
Copy-Item (Join-Path $repo "profiles") (Join-Path $dist "profiles") -Recurse -Force

$zip = Join-Path $repo "desktop\out\dist\FilmLab-portable.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Write-Host "Zipping portable build: $zip"
Compress-Archive -Path $dist -DestinationPath $zip

Write-Host "Done. Portable folder: $dist"
Write-Host "Portable zip:          $zip"
Write-Host "Installer (optional):  ISCC.exe $($PSScriptRoot)\FilmLab.iss"
