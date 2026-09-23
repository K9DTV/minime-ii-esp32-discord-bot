# MiniMe II — HIL soak results

Attested local soaks (GitHub Actions cannot reach the board). Playbook: [`HIL_SOAK.md`](HIL_SOAK.md). Raw log for the run below lived in `docs/lan-monitor.log` on the soak PC (gitignored).

## 2026-09-22 / 2026-09-23 — v0.7.43 TWDT + `!ask` hammer

| Field | Value |
|---|---|
| Firmware | **v0.7.43** (`Display · v0.7.43`) |
| Commit | `a73fa11` — 90 s loop TWDT, Discord send 60 s budget, `!ask` handshake 15 s / body 30 s |
| Board | Guition JC3248W535EN @ `http://192.168.68.60` |
| Monitor | `docs/lan-monitor.ps1` (default base URL) |
| Monitor wall start | 2026-09-22 **15:15** PDT |
| Attestation time | 2026-09-23 ~**00:19** PDT |
| Result | **PASS** |

### What was under test

- Loop-task TWDT at **90 s** (`enableLoopWDT` only; no `uiTask` subscribe; no mid-HTTPS TWDT reset sprinkle)
- `sendDiscordMessage` **60 s** wall budget
- DeepSeek `!ask` timeouts: handshake **15 s**, body deadline **30 s**
- Dual-core HOL path (dedicated DeepSeek TLS)

### Board continuous uptime

- Intentional **software-update reboot** ~**15:55** PDT (not a panic). Uptime reset; Gateway down **3 s**, then `READY`.
- Continuous post-update uptime reached **~7h 45m+** by **23:42** (still climbing; no further uptime reset in the log through attestation).

### Gateway outages (`GW_DOWN` via lan-monitor)

| When (PDT) | Duration | Cause |
|---|---:|---|
| 15:55:36 → 15:55:39 | **3 s** | Software update reboot |
| 22:56:23 → 22:56:28 | **5 s** | Brief drop; recovered; `READY` ~1 min later |

- **Total GW-down:** ~**8 s**
- **No** `GW_DOWN` lasting ≥60 s
- **No** `MONITOR_STOP`

### LAN poll timeouts (`FETCH_FAIL`)

Seven LAN `/api/status` timeouts (monitor HTTP timeout). **Not** Discord Gateway outages. One fell during the `!ask` hammer (23:16) — expected when Core 1 is busy on HTTPS.

### `!ask` stress

| Field | Value |
|---|---|
| Window | **23:10 → 00:16** PDT (hammered repeatedly) |
| Discord message drops | **None** (operator report) |
| `GW_DOWN` in window | **None** |
| Reboot / TWDT panic | **None** (uptime kept increasing) |
| Heap (samples) | Stable ~**37–38%** |

### Verdict

**v0.7.43 TWDT + ask budgets are board-proven** under multi-hour soak and ~66 minutes of repeated `!ask`. Soft Discord guard (HB ack) + 90 s loop backstop held without false panic. GitHub CI green still does not replace HIL; this file is the HIL attestation for this release.
