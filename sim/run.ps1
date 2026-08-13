$ErrorActionPreference = 'Stop'

& (Join-Path $PSScriptRoot 'build.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$executable = Join-Path $PSScriptRoot 'build\Release\smart-grind-sim.exe'
& $executable
