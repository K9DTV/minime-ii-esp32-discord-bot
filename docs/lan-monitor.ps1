# MiniMe II LAN monitor - stop only if gw=false for >=60s. FETCH_FAIL logged, no stop.
# See docs/HIL_SOAK.md
param(
  [string]$BaseUrl = $(if ($env:MINIME_LAN) { $env:MINIME_LAN } else { "http://192.168.68.60" }),
  [string]$LogPath = ""
)
$ErrorActionPreference = "Continue"
$base = $BaseUrl.TrimEnd("/")
if (-not $LogPath) {
  $LogPath = Join-Path $PSScriptRoot "lan-monitor.log"
}
$out = $LogPath
$dir = Split-Path $out
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }

function Parse-Vbus([object]$v) {
  if ($null -eq $v) { return $null }
  $s = "$v" -replace '[^\d\.\-]', ''
  if ($s -eq '') { return $null }
  try { return [double]$s } catch { return $null }
}

"=== MiniMe II monitor start $(Get-Date -Format o) base=$base; stop only gw=false >=60s (FETCH_FAIL log only) ===" | Out-File -FilePath $out -Encoding utf8

$prevLog = $null
$prevGw = $null
$prevBot = $null
$prevCpu = $null
$failSince = $null
$lastVbus = $null

while ($true) {
  $ts = Get-Date -Format "HH:mm:ss"
  $now = Get-Date
  $failing = $false
  $j = $null

  try {
    $r = Invoke-WebRequest -Uri "$base/api/status?t=$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())" -TimeoutSec 5 -UseBasicParsing
    $j = $r.Content | ConvertFrom-Json
    $vb = Parse-Vbus $j.vbus
    if ($null -ne $vb) { $lastVbus = $vb }
    if (-not [bool]$j.gw) {
      $failing = $true
    }
  } catch {
    $cause = if ($null -ne $lastVbus) { "vbus_last_good=$([math]::Round($lastVbus,3)) V" } else { "vbus=unknown" }
    Add-Content -Path $out -Value "[$ts] FETCH_FAIL $($_.Exception.Message) | cause: $cause (LAN only; not Discord stop)"
  }

  if ($failing) {
    if ($null -eq $failSince) {
      $failSince = $now
      $vb = Parse-Vbus $j.vbus
      $vc = if ($null -ne $vb) { "vbus_now=$([math]::Round($vb,3)) V" } else { "vbus=unknown" }
      Add-Content -Path $out -Value "[$ts] FAIL_START kind=GW_DOWN | cause: $vc"
      if ($null -ne $j) {
        Add-Content -Path $out -Value "[$ts] gw=$($j.gw) botOnline=$($j.botOnline) cpu=$($j.cpuMhz) rssi=$($j.rssi) up=$($j.uptime) lcd=$($j.lcd) heapPct=$($j.heapPct) vbus=$($j.vbus)"
      }
    } else {
      $dur = ($now - $failSince).TotalSeconds
      if ($dur -ge 60) {
        Add-Content -Path $out -Value "=== MONITOR_STOP rule: GW_DOWN lasted $([int]$dur)s (>=60s); stopped $(Get-Date -Format o) ==="
        break
      }
    }
  } else {
    if ($null -ne $failSince) {
      $dur = [int](($now - $failSince).TotalSeconds)
      $vb = Parse-Vbus $j.vbus
      $vc = if ($null -ne $vb) { "vbus_now=$([math]::Round($vb,3)) V" } else { "vbus=unknown" }
      Add-Content -Path $out -Value "[$ts] FAIL_CLEARED kind=GW_DOWN after=${dur}s | cause: $vc"
      if ($null -ne $j) {
        Add-Content -Path $out -Value "[$ts] gw=$($j.gw) botOnline=$($j.botOnline) cpu=$($j.cpuMhz) rssi=$($j.rssi) up=$($j.uptime) lcd=$($j.lcd) heapPct=$($j.heapPct) vbus=$($j.vbus)"
      }
      $failSince = $null
    }
  }

  if ($null -ne $j) {
    $gw = [bool]$j.gw
    $bot = [bool]$j.botOnline
    $cpu = $j.cpuMhz
    $logTail = $null
    if ($j.PSObject.Properties.Name -contains "log" -and $j.log) {
      $logTail = ($j.log | Select-Object -Last 1)
    } elseif ($j.PSObject.Properties.Name -contains "fulllog" -and $j.fulllog) {
      $lines = ($j.fulllog -split "`n") | Where-Object { $_.Trim() -ne "" }
      $logTail = $lines | Select-Object -Last 1
    }

    $changed = ($null -eq $prevGw) -or ($gw -ne $prevGw) -or ($bot -ne $prevBot) -or ($cpu -ne $prevCpu)
    if ($changed) {
      Add-Content -Path $out -Value "[$ts] gw=$gw botOnline=$bot cpu=$cpu rssi=$($j.rssi) up=$($j.uptime) lcd=$($j.lcd) heapPct=$($j.heapPct) vbus=$($j.vbus)"
      $prevGw = $gw
      $prevBot = $bot
      $prevCpu = $cpu
    }

    if ($null -ne $logTail -and $logTail -ne $prevLog) {
      if ($logTail -match "DROP|RECOVER|DISCONNECT|OP7|READY|VBUS") {
        Add-Content -Path $out -Value "[$ts] LOG   $logTail"
      }
      $prevLog = $logTail
    }
  }

  Start-Sleep -Seconds 3
}
