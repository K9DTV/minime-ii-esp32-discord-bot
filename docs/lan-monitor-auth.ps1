# MiniMe II LAN monitor with WEB_UI_PASSWORD login (X-MiniMe-Token).
# Login once, reuse token; re-login only on 401. Avoids reminting session every poll.
param(
  [string]$BaseUrl = $(if ($env:MINIME_LAN) { $env:MINIME_LAN } else { "http://192.168.68.60" }),
  [string]$PassFile = (Join-Path $PSScriptRoot ".web_ui_pass"),
  [string]$LogPath = "",
  [int]$IntervalSec = 15
)
$ErrorActionPreference = "Continue"
$base = $BaseUrl.TrimEnd("/")
if (-not $LogPath) { $LogPath = Join-Path $PSScriptRoot "lan-monitor.log" }
if ($IntervalSec -lt 2) { $IntervalSec = 2 }

function Get-MiniMeToken([string]$Base, [string]$Pass) {
  $body = "pass=" + [uri]::EscapeDataString($Pass)
  $login = Invoke-WebRequest -Uri "$Base/api/login" -Method POST -Body $body -ContentType "application/x-www-form-urlencoded" -TimeoutSec 8 -UseBasicParsing
  $lj = $login.Content | ConvertFrom-Json
  if ($lj.token) { return [string]$lj.token }
  return $null
}

function Parse-Vbus([object]$v) {
  if ($null -eq $v) { return $null }
  $s = "$v" -replace '[^\d\.\-]', ''
  if ($s -eq '') { return $null }
  try { return [double]$s } catch { return $null }
}

$pass = $null
if (Test-Path $PassFile) {
  $pass = (Get-Content -Raw $PassFile).Trim()
  Remove-Item -Force $PassFile -ErrorAction SilentlyContinue
} elseif ($env:MINIME_WEB_UI_PASSWORD) {
  $pass = [string]$env:MINIME_WEB_UI_PASSWORD
}
if (-not $pass) {
  "=== MONITOR_STOP no password $(Get-Date -Format o) ===" | Out-File -FilePath $LogPath -Encoding utf8 -Append
  exit 1
}

"=== MiniMe II monitor start $(Get-Date -Format o) base=$base; auth=on login-once; interval=${IntervalSec}s; stop only gw=false >=60s ===" | Out-File -FilePath $LogPath -Encoding utf8
$token = $null
try {
  $token = Get-MiniMeToken $base $pass
} catch {
  Add-Content -Path $LogPath -Value "=== MONITOR_STOP login failed $($_.Exception.Message) $(Get-Date -Format o) ==="
  exit 1
}
if (-not $token) {
  Add-Content -Path $LogPath -Value "=== MONITOR_STOP empty token $(Get-Date -Format o) ==="
  exit 1
}
Add-Content -Path $LogPath -Value "[$((Get-Date).ToString('HH:mm:ss'))] LOGIN_OK token_reuse=on"

$prevLog = $null; $prevGw = $null; $prevBot = $null; $prevCpu = $null; $failSince = $null; $lastVbus = $null; $fetchFailSince = $null
while ($true) {
  $ts = Get-Date -Format "HH:mm:ss"
  $now = Get-Date
  $failing = $false
  $j = $null
  try {
    $headers = @{ "X-MiniMe-Token" = $token }
    try {
      $r = Invoke-WebRequest -Uri "$base/api/status?t=$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())" -Headers $headers -TimeoutSec 5 -UseBasicParsing
    } catch {
      $code = $null
      if ($_.Exception.Response) { $code = [int]$_.Exception.Response.StatusCode }
      if ($code -eq 401) {
        $token = Get-MiniMeToken $base $pass
        if (-not $token) { throw "relogin empty token" }
        Add-Content -Path $LogPath -Value "[$ts] RELOGIN_OK"
        $headers = @{ "X-MiniMe-Token" = $token }
        $r = Invoke-WebRequest -Uri "$base/api/status?t=$([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())" -Headers $headers -TimeoutSec 5 -UseBasicParsing
      } else { throw }
    }
    $j = $r.Content | ConvertFrom-Json
    $vb = Parse-Vbus $j.vbus
    if ($null -ne $vb) { $lastVbus = $vb }
    if (-not [bool]$j.gw) { $failing = $true }
    $fetchFailSince = $null
  } catch {
    $cause = if ($null -ne $lastVbus) { "vbus_last_good=$([math]::Round($lastVbus,3)) V" } else { "vbus=unknown" }
    Add-Content -Path $LogPath -Value "[$ts] FETCH_FAIL $($_.Exception.Message) | cause: $cause (LAN only; not Discord stop)"
    if ($null -eq $fetchFailSince) { $fetchFailSince = $now }
    elseif (($now - $fetchFailSince).TotalSeconds -ge 120) {
      Add-Content -Path $LogPath -Value "=== MONITOR_STOP fetch failed >=120s $(Get-Date -Format o) ==="
      break
    }
  }
  if ($failing) {
    if ($null -eq $failSince) {
      $failSince = $now
      Add-Content -Path $LogPath -Value "[$ts] FAIL_START kind=GW_DOWN"
      if ($null -ne $j) { Add-Content -Path $LogPath -Value "[$ts] gw=$($j.gw) botOnline=$($j.botOnline) cpu=$($j.cpuMhz) rssi=$($j.rssi) up=$($j.uptime) lcd=$($j.lcd) heapPct=$($j.heapPct) ver=$($j.ver)" }
    } else {
      $dur = ($now - $failSince).TotalSeconds
      if ($dur -ge 60) {
        Add-Content -Path $LogPath -Value "=== MONITOR_STOP rule: GW_DOWN lasted $([int]$dur)s (>=60s); stopped $(Get-Date -Format o) ==="
        break
      }
    }
  } else {
    if ($null -ne $failSince) {
      $dur = [int](($now - $failSince).TotalSeconds)
      Add-Content -Path $LogPath -Value "[$ts] FAIL_CLEARED kind=GW_DOWN after=${dur}s"
      $failSince = $null
    }
  }
  if ($null -ne $j) {
    $gw = [bool]$j.gw; $bot = [bool]$j.botOnline; $cpu = $j.cpuMhz
    $logTail = $null
    if ($j.PSObject.Properties.Name -contains "log" -and $j.log) { $logTail = ($j.log | Select-Object -Last 1) }
    elseif ($j.PSObject.Properties.Name -contains "fulllog" -and $j.fulllog) {
      $lines = ($j.fulllog -split "`n") | Where-Object { $_.Trim() -ne "" }
      $logTail = $lines | Select-Object -Last 1
    }
    Add-Content -Path $LogPath -Value "[$ts] gw=$gw presence=$(if ($bot) { 'online' } else { 'idle' }) cpu=$cpu rssi=$($j.rssi) up=$($j.uptime) lcd=$($j.lcd) heapPct=$($j.heapPct) ver=$($j.ver)"
    $prevGw = $gw; $prevBot = $bot; $prevCpu = $cpu
    if ($null -ne $logTail -and $logTail -ne $prevLog) {
      if ($logTail -notmatch 'DS18') {
        if ($logTail -match 'DROP|RECOVER|DISCONNECT|OP7|READY|RESUME|GW_REBIND|CONNECT_AT|SENT_IDENTIFY|VBUS') {
          Add-Content -Path $LogPath -Value "[$ts] LOG   $logTail"
        }
      }
      $prevLog = $logTail
    }
  }
  Start-Sleep -Seconds $IntervalSec
}


