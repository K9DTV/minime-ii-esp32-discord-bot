# One-shot MiniMe II HIL ops snapshot (login once, status once, exit).
param(
  [string]$BaseUrl = $(if ($env:MINIME_LAN) { $env:MINIME_LAN } else { "http://192.168.68.60" }),
  [string]$PassFile = (Join-Path $PSScriptRoot ".web_ui_pass"),
  [string]$OutJson = (Join-Path $PSScriptRoot "lan-status-snapshot.json")
)
$ErrorActionPreference = "Stop"
$base = $BaseUrl.TrimEnd("/")
if (-not (Test-Path $PassFile)) { Write-Error "missing pass file $PassFile"; exit 2 }
$pass = (Get-Content -Raw $PassFile).Trim()
Remove-Item -Force $PassFile -ErrorAction SilentlyContinue
$body = "pass=" + [uri]::EscapeDataString($pass)
$login = Invoke-WebRequest -Uri "$base/api/login" -Method POST -Body $body -ContentType "application/x-www-form-urlencoded" -TimeoutSec 8 -UseBasicParsing
$token = [string](($login.Content | ConvertFrom-Json).token)
if (-not $token) { Write-Error "no token"; exit 1 }
$headers = @{ "X-MiniMe-Token" = $token }
$r = Invoke-WebRequest -Uri "$base/api/status?t=$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())" -Headers $headers -TimeoutSec 5 -UseBasicParsing
Set-Content -Path $OutJson -Value $r.Content -Encoding utf8
$j = $r.Content | ConvertFrom-Json
Write-Output ("ver={0} gw={1} botOnline={2} cpu={3} rssi={4} up={5} lcd={6} heapPct={7}" -f $j.ver,$j.gw,$j.botOnline,$j.cpuMhz,$j.rssi,$j.uptime,$j.lcd,$j.heapPct)
