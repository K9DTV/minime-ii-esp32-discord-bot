# MiniMe II — local HIL soak (LAN)

GitHub Actions **cannot** talk to your Guition board. Use this playbook on the PC that shares Wi‑Fi with MiniMe II.

## Preconditions

- Board flashed with the build under test (`Display · vX.Y.Z` matches `VERSION`)
- LAN page responds: `http://<board-ip>/` and `/api/status`
- Discord Gateway usually **Good** before you start

## Soak (Gateway watch)

From the `MinimeII` repo root (PowerShell):

```powershell
# Optional: set board URL (default is in the script)
$env:MINIME_LAN = "http://192.168.x.x"
powershell -File docs/lan-monitor.ps1
```

Or:

```powershell
powershell -File docs/lan-monitor.ps1 -BaseUrl "http://192.168.x.x"
```

Behavior:

- Polls `/api/status` on a short interval
- Logs to `docs/lan-monitor.log`
- Stops only if `gw` stays false for **≥ 60 s**
- `FETCH_FAIL` (LAN blip) is logged; does not stop the soak by itself

## Suggested soak checklist

1. Idle 10+ minutes — GW stays Good; note any `HB_ACK_TIMEOUT` / DROP in LAN Serial/LOG
2. `!weather` / `!news` / `!ask` while watching GW
3. Touch wake after backlight sleep
4. Owner `!sys` — `MmLog Core0 drops` should stay low unless the Core0 ring overflows
5. Optional OTA once — after reboot, confirm version + GW again

## What CI covers instead

| Layer | Where | Proves |
|---|---|---|
| Host sanity | workflow **Sanity** / `ci_sanity.py` | VERSION sync, one `.ino`, no AJ6 types, secrets not tracked |
| Python | workflow **Python** / pytest | Pillow logo helper + sanity via tests |
| HTML | workflow **HTML** / `ci_html.py` | LAN CSS/JS brace + required IDs; SVG headers |
| Compile | workflow **Compile** | Sketch builds for the Guition FQBN |
| HIL soak | This doc + `lan-monitor.ps1` | Live Discord/Wi‑Fi/LCD behavior |
