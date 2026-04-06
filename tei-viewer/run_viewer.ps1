# TEI viewer: local static server (Windows; same role as run_viewer.sh)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Set-Location $PSScriptRoot

$port = 8765
if ($args.Count -ge 1) {
  $parsed = 0
  if (-not [int]::TryParse($args[0], [ref]$parsed)) {
    Write-Error "Invalid port: $($args[0])"
    exit 1
  }
  $port = $parsed
}

Write-Host "Serving TEI viewer at: http://127.0.0.1:$port/"
Write-Host 'Press Ctrl+C to stop.'
Write-Host ''

if (Get-Command py -ErrorAction SilentlyContinue) {
  py -3 -m http.server $port
  exit $LASTEXITCODE
}
if (Get-Command python -ErrorAction SilentlyContinue) {
  python -m http.server $port
  exit $LASTEXITCODE
}
if (Get-Command python3 -ErrorAction SilentlyContinue) {
  python3 -m http.server $port
  exit $LASTEXITCODE
}

Write-Error 'No Python found (tried py -3, python, python3). Install Python 3 and add it to PATH.'
exit 1
