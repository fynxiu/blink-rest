$ErrorActionPreference = 'Stop'

$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$executable = Join-Path $root 'build\Debug\blinkrest_win32.exe'
$screenshot = Join-Path $root 'overlay-smoke.png'
$harness = Join-Path $PSScriptRoot 'interactive_overlay_smoke.ps1'

& $harness -Executable $executable -Screenshot $screenshot
exit $LASTEXITCODE
