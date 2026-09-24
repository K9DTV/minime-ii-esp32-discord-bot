# MiniMe II — soak for v0.7.80 (post-split + heap + Controls)

Flash **v0.7.80** (`Display · v0.7.80`). General Gateway watch still uses [`HIL_SOAK.md`](HIL_SOAK.md) + `docs/lan-monitor.ps1`. This playbook is the **extra** checklist for the 0.7.79–0.7.80 batch.

## What this soak is proving

| Change | Pass means |
|---|---|
| Fixed-buffer Discord REST / DeepSeek | Posts and `!ask` work; no heap-stall panic mid-post |
| `web_ui` / `web_render` / `discord_http` split | Same LAN page + Discord behavior as before the split |
| Web slider debounce (75 ms) | Dragging bright/vol does not flood `/api/controls`; final value sticks |
| Controls hit boxes (paint-side) | After Menus → Controls, sliders/toggles/Cancel/Save hit correctly |
| Recall early-out / factory reset | Cancel when clean is quiet; long-press Cancel / `!resetprefs` restores defaults |

## Setup

```powershell
$env:MINIME_LAN = "http://192.168.x.x"   # your board
powershell -File docs/lan-monitor.ps1
```

Leave the monitor running in one window. Do the steps below in Discord + on the glass / phone.

## Timed Gateway soak (background)

- Idle **≥ 30 min** with monitor up — no `MONITOR_STOP`, no GW down ≥ 60 s
- Note any `FETCH_FAIL` (LAN only) vs real Discord drops

## A — Discord REST / `!ask` (fixed buffers)

1. `!help` — reply lands; LCD shows Sent
2. `!weather <zip>` — OK; GW stays Good
3. `!sys` — heap/PSRAM lines look sane; cmd-error ring not exploding
4. `!ask` short question — reply posts once (≤2000 chars)
5. `!ask` longer question (~400–500 chars) — still one Discord message; no board hang
6. Hammer: **5×** `!ask` in ~2 min while watching Serial/LOG — no panic, GW recovers if busy

## B — LAN web after split + debounce

1. Open `http://<board-ip>/` — Display metrics|users load; version **0.7.80**
2. Menus → **Log** — LOG | Serial fill; no blank panels
3. Menus → **Controls** — sliders + toggles + Cancel/Save dogs
4. Drag **Brightness** quickly end-to-end — LCD backlight follows; only a few POSTs (debounce), final % matches glass
5. Drag **Volume** same way — tick volume follows when you tap
6. Toggle Sound / Ticks / Notify — each click posts once; glass matches
7. **Save** — dirty write; reboot (or power cycle) — prefs survive
8. Change bright, **Cancel** (short) — recalls; if you Save first then Cancel with no edits, no flash thrash (early-out)

## C — LCD Controls hit boxes

1. Menus → Controls (force full paint)
2. Drag brightness track — changes
3. Drag volume track — changes
4. Tap Sound / Ticks / Notify — toggle
5. Short Cancel — recall; long-press Cancel ~3 s — factory defaults (bright/vol 100%, toggles ON)
6. Owner Discord: `!resetprefs` — same factory result

## D — Smoke that nothing else broke

1. Touch wake after backlight sleep
2. Light/Dark chip on glass — LCD only (web theme independent)
3. DM or @mention — alert + alarm if Notify on; `!clear` clears
4. Optional: owner `!ota` shows IP/host/3232 (do **not** require a full OTA for PASS)

## Pass / Fail

| Result | Rule |
|---|---|
| **PASS** | A–D done; monitor ≥30 min with no GW≥60 s stop; version on glass = 0.7.80 |
| **FAIL** | Panic, stuck GW, Controls miss-hits after layout entry, web Controls wrong after Save, `!ask`/REST hang |

Record the run in [`soak-results.md`](soak-results.md) (date, commit, PASS/FAIL, notes).
