# Runs a UI script against the Release build, offscreen, with a throwaway catalog.
# Usage: powershell -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/smoke.script [-AppArgs @('--lightroom-edit','x.tif')] [-Ui v2|gallery]
# Scripts may use ${SAMPLE_A}, ${SAMPLE_B}, ${SAMPLE_TIFF}, ${TEMP} placeholders.
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string[]]$AppArgs = @(),
    [string]$SampleA = "E:/new_raws/3071874357.arw",
    [string]$SampleB = "E:/new_raws/7033866904.arw",
    [string]$SampleTiff = "E:/new_raws/3071874357.tif",
    [int]$TimeoutSec = 120,
    [string]$Exe = "",
    [string]$Ui = "",
    [string]$UiSettings = ""
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$exe = if ($Exe) { $Exe } else { Join-Path $repo "desktop\out\build\Release\DFEE.exe" }
$work = Join-Path ([IO.Path]::GetTempPath()) ("filmlab-ui-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $work | Out-Null
[IO.File]::WriteAllText((Join-Path $work "not-an-image.arw"), "not a raw file")
# Exports default to a throwaway folder; "taken.jpg" lets scripts check the taken-name rules.
New-Item -ItemType Directory -Force (Join-Path $work "exports") | Out-Null
[IO.File]::WriteAllText((Join-Path $work "exports\taken.jpg"), "")
$env:DFEE_EXPORT_DIR = Join-Path $work "exports"
$tempTiff = Join-Path $work "lr-working.tif"
if (Test-Path $SampleTiff) { Copy-Item $SampleTiff $tempTiff }
$body = (Get-Content $Script -Raw).Replace('${SAMPLE_A}', $SampleA).Replace('${SAMPLE_B}', $SampleB).
    Replace('${SAMPLE_TIFF}', ($tempTiff -replace '\\', '/')).Replace('${TEMP}', ($work -replace '\\', '/'))
$scriptFile = Join-Path $work "script.txt"
[IO.File]::WriteAllText($scriptFile, $body)
$resolvedArgs = $AppArgs | ForEach-Object { $_.Replace('${SAMPLE_TIFF}', $tempTiff) }

$env:QT_QPA_PLATFORM = "offscreen"
$env:FILMLAB_UI = $Ui
# v2 inspector state goes to a throwaway ini unless a test shares one across runs.
$env:DFEE_UI_SETTINGS = if ($UiSettings) { $UiSettings } else { Join-Path $work "ui-settings.ini" }
$env:DFEE_PRESETS_DIR = Join-Path $work "presets"
$env:DFEE_UI_SCRIPT = "@" + $scriptFile
$env:DFEE_CATALOG_PATH = Join-Path $work "catalog.sqlite"
$env:PATH = (Join-Path $repo "cpp_engine\out\build\windows-msvc-vcpkg\vcpkg_installed\x64-windows\bin") + ";" + $env:PATH
# Start-Process rejects an empty -ArgumentList, so only pass it when there are arguments.
$proc = if ($resolvedArgs) { Start-Process -FilePath $exe -ArgumentList $resolvedArgs -PassThru }
        else { Start-Process -FilePath $exe -PassThru }
if (-not $proc.WaitForExit($TimeoutSec * 1000)) { $proc.Kill(); Write-Host "TIMEOUT"; exit 4 }
$log = Get-ChildItem "$env:LOCALAPPDATA\Film Lab\Film Lab\Logs" | Sort-Object LastWriteTime | Select-Object -Last 1
Get-Content $log.FullName | Select-String "UISCRIPT" | ForEach-Object { $_.Line.Substring(11) }
Write-Host "exit=$($proc.ExitCode) work=$work"
exit $proc.ExitCode
