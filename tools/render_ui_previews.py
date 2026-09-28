#!/usr/bin/env python3
"""Render MiniMe II LCD and LAN web previews (dark/light x Display/Log/Controls).

Shots use the firmware web CSS/JS and SVG headers, plus a 480x320 LCD mock
that follows display_layout.h. Writes PNGs under docs/ui-preview/.

  pip install -r tools/requirements.txt -r tools/requirements-logo.txt
  playwright install chromium
  python tools/render_ui_previews.py
"""
from __future__ import annotations

import asyncio
import json
import re
import sys
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "MiniMe_Discord_Bot_II"
OUT = ROOT / "docs" / "ui-preview"
WORK = ROOT / "docs" / "ui-preview" / "_build"

PAGES = ("display", "log", "controls")
THEMES = ("dark", "light")


def extract_all(text: str, marker: str) -> list[str]:
    return re.findall(rf'R"{marker}\((.*?)\){marker}"', text, re.S)


def read_header(name: str) -> str:
    return (SKETCH / name).read_text(encoding="utf-8", errors="replace")


def write_assets() -> None:
    WORK.mkdir(parents=True, exist_ok=True)
    assets = read_header("web_assets.h")
    css = extract_all(assets, "CSS")
    js = extract_all(assets, "JS")
    if len(css) != 1 or len(js) < 2:
        raise SystemExit("web_assets.h: expected 1 CSS block and boot+app JS")
    (WORK / "ui.css").write_text(css[0], encoding="utf-8")
    (WORK / "boot.js").write_text(js[0], encoding="utf-8")
    (WORK / "ui.js").write_text(js[1], encoding="utf-8")

    svg_files = {
        "logo.svg": ("k9dtv_logo_svg.h", 0),
        "logo-bright.svg": ("k9dtv_logo_bright_svg.h", 0),
        "logo-spin.svg": ("k9dtv_logo_spin_svg.h", 0),
        "logo-spin-bright.svg": ("k9dtv_logo_spin_bright_svg.h", 0),
        "chip.svg": ("menu_chip_svg.h", 0),
        "chip-bright.svg": ("menu_chip_svg.h", 1),
        "mark-left.svg": ("k9_mark_icon_svg.h", 0),
        "mark-left-bright.svg": ("k9_mark_icon_bright_svg.h", 0),
        "mark-right.svg": ("k9_mark_icon_right_svg.h", 0),
        "mark-right-bright.svg": ("k9_mark_icon_right_bright_svg.h", 0),
    }
    for out_name, (header, idx) in svg_files.items():
        blocks = extract_all(read_header(header), "SVG")
        if idx >= len(blocks):
            raise SystemExit(f"{header}: missing SVG block {idx}")
        (WORK / out_name).write_text(blocks[idx], encoding="utf-8")

    (WORK / "status.json").write_text(json.dumps(STATUS), encoding="utf-8")
    (WORK / "index.html").write_text(WEB_HTML, encoding="utf-8")
    (WORK / "lcd.html").write_text(LCD_HTML, encoding="utf-8")


STATUS = {
    "gw": 1,
    "botOnline": True,
    "time": "10:42 PM",
    "date": "Wed Sep 23",
    "uptime": "2d 4h 12m",
    "tempOk": True,
    "tempF": 72,
    "tempC": 22,
    "rssi": -55,
    "sigPct": 75,
    "psramTotal": 8388608,
    "psramFree": 7372800,
    "psramPct": 88,
    "heapFree": 184320,
    "heapPct": 55,
    "sdOk": True,
    "sdFreeMb": 512,
    "sdPct": 64,
    "usersActive": 3,
    "usersMax": 22,
    "httpsBusy": False,
    "identified": True,
    "dm": False,
    "mention": False,
    "lastEvent": "READY",
    "secretsFromSd": False,
    "ip": "192.168.68.60",
    "wifiOk": True,
    "ota": "minime2.local",
    "ver": "1.00.00",
    "cpuMhz": 240,
    "lcd": "awake",
    "dashFlushMs": 12,
    "dashDrawMs": 18,
    "lcdMsg": "hello from the glass",
    "msg1": "",
    "msg2": "",
    "bright": 80,
    "vol": 60,
    "notify": True,
    "ticks": True,
    "sound": True,
    "users": (
        [
            {"name": "dogma", "status": "Online", "bot": "12"},
            {"name": "alice", "status": "Idle", "bot": "3"},
            {"name": "bob", "status": "DND", "bot": "1"},
        ]
        + [{"name": "---", "status": "Off", "bot": "0"} for _ in range(19)]
    ),
    "fulllog": [
        "[GW] drop episode begin",
        "[GW] reconnect try 1/3",
        "[GW] WS open",
        "[GW] HELLO",
        "[GW] IDENTIFY",
        "[GW] READY guilds=2",
        "[HB] ack ok",
        "[CMD] !sys from dogma",
        "[HTTPS] idle",
        "[LOG] ring 55",
        "[MM] Core0 bridge drain",
        "[OTA] idle",
        "[SYS] uptime ok",
        "[GW] PRESENCE_UPDATE",
        "[CMD] !msg hello from the glass",
    ],
    "serial": [
        "[boot] MiniMe-II 1.00.00",
        "[WiFi] connected",
        "[GW] identified",
        "[UI] dash flush 12ms",
        "[UI] dash draw 18ms",
        "[CMD] !help",
        "[SD] mounted",
        "[HTTPS] idle",
        "MmLog line",
        "MmLog line",
        "MmLog line",
        "MmLog line",
        "MmLog line",
        "MmLog line",
        "MmLog line",
    ],
}

WEB_HTML = """<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MiniMe-II</title>
<script src="/boot.js"></script>
<link rel="stylesheet" href="/ui.css">
<style>main{min-height:0 !important;height:auto !important}</style>
</head><body><main>
<div class="top"><div class="top-row">
<button type="button" id="theme-toggle" class="theme-chip-trigger" aria-pressed="false" aria-label="Switch to light mode">
<img class="menu-chip-icon" id="theme-chip-img" src="/chip.svg" width="64" height="64" alt="">
<span class="menu-chip-label" aria-hidden="true"><span class="theme-toggle-glyph" id="theme-chip-glyph">&#9728;</span>
<span class="theme-toggle-text" id="theme-chip-text">Light</span></span></button>
<header class="brand"><a class="logo-link" href="https://k9dtv.com">
<img class="logo" id="brand-logo" src="/logo.svg" alt="K9DTV"></a></header>
<button type="button" id="layout-toggle" class="theme-chip-trigger" aria-pressed="false" aria-label="Cycle display log controls">
<span class="menu-chip-menus" aria-hidden="true">Menus</span>
<img class="menu-chip-icon" id="layout-chip-img" src="/chip.svg" width="64" height="64" alt="">
<span class="menu-chip-label" aria-hidden="true"><span class="theme-toggle-text" id="layout-chip-text">Display</span></span></button>
</div><p class="sub" id="brand-sub">MiniMe-II A Discord Bot</p></div>
<div class="layout">
<section class="box" id="box-metrics"><h2>Display  -  v1.00.00</h2><div id="metrics" class="dash muted">Loading...</div></section>
<section class="box" id="box-users"><h2>Users</h2><div id="users" class="users muted">Loading...</div></section>
<section class="box" id="box-logfile"><h2>LOG</h2><div id="logfile" class="serial"><div class="empty">Waiting...</div></div></section>
<section class="box" id="box-serial"><h2>Serial</h2><div id="serial" class="serial"><div class="empty">Waiting...</div></div></section>
<section class="box box-lcd-ctrl" id="box-ctrl-sliders"><div class="ctrl-panel">
<div class="ctrl-title">Controls</div>
<div class="ctrl-slot"><div class="ctrl-row"><div class="ctrl-lab" id="ctrl-bright-lab">Brightness 80%</div>
<div class="ctrl-track" id="ctrl-bright-track" style="--pct:80"><div class="ctrl-fill"></div><div class="ctrl-knob"></div>
<input id="ctrl-bright" type="range" min="0" max="100" value="80" aria-label="Brightness"></div></div></div>
<div class="ctrl-slot"><div class="ctrl-row"><div class="ctrl-lab" id="ctrl-vol-lab">Volume 60%</div>
<div class="ctrl-track" id="ctrl-vol-track" style="--pct:60"><div class="ctrl-fill"></div><div class="ctrl-knob"></div>
<input id="ctrl-vol" type="range" min="0" max="100" value="60" aria-label="Volume"></div></div></div>
<div class="ctrl-slot"><button type="button" class="ctrl-clear-btn" id="ctrl-clear">Clear DM/Mention/Msg</button></div>
<button type="button" class="dog-btn dog-cancel" id="ctrl-cancel" aria-label="Cancel">
<img id="dog-left-img" width="72" height="48" alt=""><span class="dog-lab">Cancel</span></button>
</div></section>
<section class="box box-lcd-ctrl" id="box-ctrl-toggles"><div class="ctrl-panel">
<div class="ctrl-title">Toggles</div>
<div class="ctrl-slot"><button type="button" class="tog" id="ctrl-sound" aria-pressed="true"><span class="lab">Sound</span><span class="st">ON</span></button></div>
<div class="ctrl-slot"><button type="button" class="tog" id="ctrl-ticks" aria-pressed="true"><span class="lab">Ticks</span><span class="st">ON</span></button></div>
<div class="ctrl-slot"><button type="button" class="tog" id="ctrl-notify" aria-pressed="true"><span class="lab">Notify</span><span class="st">ON</span></button></div>
<button type="button" class="dog-btn dog-save" id="ctrl-save" aria-label="Save">
<img id="dog-right-img" width="72" height="48" alt=""><span class="dog-lab">Save</span></button>
</div></section>
<div id="err" class="err" hidden></div>
<div id="auth-gate" class="auth-gate" hidden></div>
</div></main>
<script>var POLL_MS=600000;</script>
<script>
(function(){
  var statusText = null;
  var orig = window.fetch;
  window.fetch = function(url, opts){
    var u = String(url);
    if (u.indexOf('/api/status') >= 0) {
      if (!statusText) statusText = fetch('/status.json').then(function(r){return r.text();});
      return statusText.then(function(t){
        return {ok:true,status:200,text:function(){return Promise.resolve(t);},json:function(){return Promise.resolve(JSON.parse(t));}};
      });
    }
    if (u.indexOf('/api/controls') >= 0 || u.indexOf('/api/login') >= 0) {
      return Promise.resolve({ok:true,status:200,json:function(){return Promise.resolve({ok:true,bright:80,vol:60,notify:true,ticks:true,sound:true});}});
    }
    return orig(url, opts);
  };
})();
</script>
<script src="/ui.js"></script>
</body></html>
"""

LCD_HTML = r"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>LCD</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: #111; }
  .lcd { width: 480px; height: 320px; position: relative; overflow: hidden;
    font-family: ui-monospace, Consolas, monospace; font-size: 10px; line-height: 10px; }
  .lcd.dark { background: #121212; color: #e0e0e0; --panel:#1a1a1a; --line:#2c2c2c; --muted:#b8b8b8; --ok:#2ecc71; --bad:#ffb020; --cyan:#5eb3ff; --bar-tr:#0a0a0a; }
  .lcd.light { background: #dde2ea; color: #0f172a; --panel:#f3f5f8; --line:#8b95a5; --muted:#334155; --ok:#14532d; --bad:#9a3412; --cyan:#005f73; --bar-tr:#ffffff; }
  .brand { position: absolute; left: 0; top: 0; width: 480px; height: 73px; }
  .logo { position: absolute; left: 123px; top: 0; width: 233px; height: 69px; display: block; }
  .chip { position: absolute; top: 12px; width: 44px; text-align: center; }
  .chip.theme { left: 40px; }
  .chip.layout { left: 396px; }
  .chip img { width: 44px; height: 44px; display: block; }
  .chip span, .menus {
    display: block; margin-top: 1px; font-size: 8px; font-family: "Segoe UI", Arial, sans-serif;
    font-weight: 600; letter-spacing: 0.04em; text-transform: uppercase; color: var(--muted); white-space: nowrap;
  }
  .menus { position: absolute; top: 1px; left: 396px; width: 44px; text-align: center; margin: 0; }
  .panel { position: absolute; top: 73px; height: 245px; background: var(--panel);
    border: 1px solid var(--line); border-radius: 4px; overflow: hidden; }
  .left { left: 4px; width: 234px; }
  .right { left: 242px; width: 234px; }
  .pad { position: absolute; left: 6px; right: 6px; top: 5px; bottom: 4px; display: flex; flex-direction: column; }
  .muted { color: var(--muted); } .ok { color: var(--ok); } .bad { color: var(--bad); } .cyan { color: var(--cyan); }
  .hdr, .spread { display: grid; grid-template-columns: 1fr auto 1fr; height: 10px; }
  .c { text-align: center; } .r { text-align: right; }
  .mline { display: grid; grid-template-columns: 40px 52px 1fr; align-items: center; height: 10px; }
  .bar { height: 8px; border: 1px solid var(--line); background: var(--bar-tr); }
  .bar > i { display: block; height: 100%; background: var(--cyan); }
  .row { height: 10px; white-space: nowrap; overflow: hidden; }
  .loose { height: 12px; }
  .sys { margin-top: 2px; }
  .sys div { display: grid; grid-template-columns: 56px 1fr; height: 10px; }
  .msg { margin-top: auto; height: 10px; }
  .users { display: flex; flex-direction: column; justify-content: space-between; height: 100%; }
  .uhead, .urow { display: grid; grid-template-columns: 100px 68px 1fr; font-size: 9px; line-height: 9px; }
  .uhead { color: var(--muted); height: 12px; flex: 0 0 auto; }
  .st { color: var(--cyan); } .bt { color: var(--muted); text-align: right; }
  .log-title { color: var(--muted); height: 12px; }
  .log-line { height: 9px; font-size: 9px; line-height: 9px; white-space: nowrap; overflow: hidden; }
  .sb { position: absolute; top: 18px; right: 4px; bottom: 6px; width: 8px; background: var(--bar-tr); border: 1px solid var(--line); }
  .sb i { display: block; width: 100%; height: 40%; margin-top: 60%; background: var(--cyan); }
  .ctrl-title { color: var(--cyan); height: 12px; }
  .slab { color: var(--muted); height: 12px; }
  .track { position: relative; height: 16px; margin: 2px 0 4px; background: var(--bar-tr); border: 1px solid var(--line); }
  .track i { position: absolute; left: 1px; top: 1px; bottom: 1px; background: var(--cyan); }
  .track b { position: absolute; top: -2px; width: 6px; height: 20px; background: var(--cyan); }
  .tog, .clear {
    height: 32px; border: 1px solid var(--line); border-radius: 4px; margin: 0 0 6px;
    display: flex; align-items: center; justify-content: space-between; padding: 0 8px;
  }
  .clear { justify-content: center; color: var(--cyan); }
  .dog { margin-top: auto; width: 72px; text-align: center; flex: 0 0 auto; }
  .dog img { width: 72px; height: 48px; display: block; }
  .dog span { color: var(--muted); font-size: 10px; }
  .dog.cancel { left: 6px; } .dog.save { right: 6px; }
</style></head><body>
<div class="lcd dark" id="lcd-display-dark"></div>
<div class="lcd light" id="lcd-display-light"></div>
<div class="lcd dark" id="lcd-log-dark"></div>
<div class="lcd light" id="lcd-log-light"></div>
<div class="lcd dark" id="lcd-controls-dark"></div>
<div class="lcd light" id="lcd-controls-light"></div>
<script>
const METRICS = `
  <div class="hdr"><span>MiniMe-II</span><span class="c ok">GW:Good</span><span class="r muted">10:42 PM</span></div>
  <div class="hdr"><span>Bot Online</span><span></span><span class="r muted">Wed Sep 23</span></div>
  <div class="row cyan">Up 2d 4h 12m  T 72 F/22 C</div>
  <div class="mline"><span class="k muted">Sig</span><span>-55 dBm</span><span class="bar"><i style="width:75%"></i></span></div>
  <div class="mline"><span class="k muted">PSRAM</span><span>7200K</span><span class="bar"><i style="width:88%"></i></span></div>
  <div class="mline"><span class="k muted">SRAM</span><span>180K</span><span class="bar"><i style="width:55%"></i></span></div>
  <div class="mline"><span class="k muted">SD</span><span>512M</span><span class="bar"><i style="width:64%"></i></span></div>
  <div class="spread loose"><span>Users:3/22</span><span class="c muted">HTTPS:idle</span><span class="r ok">Id:yes</span></div>
  <div class="spread"><span class="muted">DM:off</span><span></span><span class="r muted">Mention:off</span></div>
  <div class="row"><span class="muted">Event:</span> READY</div>
  <div class="sys">
    <div><span class="k">Src</span><span>firmware</span></div>
    <div><span class="k">IP</span><span>192.168.68.60</span></div>
    <div><span class="k">OTA</span><span>minime2.local</span></div>
    <div><span class="k">Ver</span><span>1.00.00</span></div>
    <div><span class="k">CPU</span><span>240 MHz</span></div>
  </div>
  <div class="spread"><span><span class="muted">LCD</span> awake</span><span></span><span class="r muted">Refresh 12/18 ms</span></div>
  <div class="msg"><span class="muted">Msg:</span> hello from the glass</div>`;
const USERS = (() => {
  const rows = [['dogma','Online','12'],['alice','Idle','3'],['bob','DND','1']];
  for (let i = 0; i < 19; i++) rows.push(['---','Off','0']);
  const body = rows.map(r => `<div class="urow"><span>${r[0]}</span><span class="st">${r[1]}</span><span class="bt">${r[2]}</span></div>`).join('');
  return '<div class="users"><div class="uhead"><span>User</span><span>Status</span><span class="bt">Bot</span></div>' + body + '</div>';
})();
const LOGS = ['[GW] drop episode begin','[GW] reconnect try 1/3','[GW] WS open','[GW] HELLO','[GW] IDENTIFY','[GW] READY guilds=2','[HB] ack ok','[CMD] !sys from dogma','[HTTPS] idle','[LOG] ring 55','[MM] Core0 bridge drain','[OTA] idle','[SYS] uptime ok','[GW] PRESENCE_UPDATE','[CMD] !msg hello from the glass','[CMD] !weather','[REST] posted','[GW] alive'];
const SERIAL = ['[boot] MiniMe-II 1.00.00','[WiFi] connected','[GW] identified','[UI] dash flush 12ms','[UI] dash draw 18ms','[CMD] !help','[SD] mounted','[HTTPS] idle','MmLog line','MmLog line','MmLog line','MmLog line','MmLog line','MmLog line','MmLog line','MmLog line','MmLog line'];
function lines(title, arr) {
  return `<div class="log-title">${title}</div>` + arr.map(l => `<div class="log-line">${l}</div>`).join('') + '<div class="sb"><i></i></div>';
}
function brand(light, page) {
  const chip = light ? '/chip-bright.svg' : '/chip.svg';
  const logo = light ? '/logo-bright.svg' : '/logo.svg';
  const themeLab = light ? ') Dark' : '* Light';
  return `<div class="brand"><div class="chip theme"><img src="${chip}" alt=""><span>${themeLab}</span></div>
    <img class="logo" src="${logo}" alt="K9DTV"><div class="menus">Menus</div>
    <div class="chip layout"><img src="${chip}" alt=""><span>${page}</span></div></div>`;
}
function controls(light) {
  const left = light ? '/mark-left-bright.svg' : '/mark-left.svg';
  const right = light ? '/mark-right-bright.svg' : '/mark-right.svg';
  const slider = (lab, pct) => `<div class="slab">${lab}</div><div class="track"><i style="width:${pct}%"></i><b style="left:calc(${pct}% - 3px)"></b></div>`;
  const tog = (lab) => `<div class="tog"><span class="muted">${lab}</span><span class="ok">ON</span></div>`;
  return `<section class="panel left"><div class="pad"><div class="ctrl-title">Controls</div>
    ${slider('Brightness 80%', 80)}${slider('Volume 60%', 60)}<div class="clear">Clear DM/Mention/Msg</div>
    <div class="dog cancel"><img src="${left}" alt=""><span>Cancel</span></div></div></section>
    <section class="panel right"><div class="pad"><div class="ctrl-title">Toggles</div>
    ${tog('Sound')}${tog('Ticks')}${tog('Notify')}
    <div class="dog save"><img src="${right}" alt=""><span>Save</span></div></div></section>`;
}
function paint(id, light, page) {
  const el = document.getElementById(id);
  let body;
  if (page === 'Display') body = `<section class="panel left"><div class="pad">${METRICS}</div></section><section class="panel right"><div class="pad">${USERS}</div></section>`;
  else if (page === 'Log') body = `<section class="panel left"><div class="pad">${lines('LOG', LOGS)}</div></section><section class="panel right"><div class="pad">${lines('Serial', SERIAL)}</div></section>`;
  else body = controls(light);
  el.innerHTML = brand(light, page) + body;
}
paint('lcd-display-dark', false, 'Display');
paint('lcd-display-light', true, 'Display');
paint('lcd-log-dark', false, 'Log');
paint('lcd-log-light', true, 'Log');
paint('lcd-controls-dark', false, 'Controls');
paint('lcd-controls-light', true, 'Controls');
</script></body></html>
"""


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(WORK), **kwargs)

    def log_message(self, fmt: str, *args) -> None:
        return


async def main() -> int:
    try:
        from playwright.async_api import async_playwright
    except ImportError:
        print("playwright required: pip install -r tools/requirements-logo.txt", file=sys.stderr)
        return 1

    write_assets()
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = httpd.server_address[1]
    import threading

    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{port}"

    async with async_playwright() as p:
        browser = await p.chromium.launch()
        context = await browser.new_context(viewport={"width": 1100, "height": 900}, device_scale_factor=2)
        for theme in THEMES:
            for layout in PAGES:
                page = await context.new_page()
                await page.add_init_script(
                    f"localStorage.setItem('k9-theme','{theme}');"
                    f"localStorage.setItem('mm-layout','{layout}');"
                )
                await page.goto(base + "/index.html", wait_until="networkidle")
                await page.wait_for_function(
                    "() => { const m = document.getElementById('metrics');"
                    " return m && m.textContent.indexOf('Loading') < 0; }"
                )
                await page.evaluate(
                    """(cfg) => { applyTheme(cfg.theme, false); applyLayout(cfg.layout, false); }""",
                    {"theme": theme, "layout": layout},
                )
                label = await page.locator("#layout-chip-text").inner_text()
                print(f"web {layout} {theme} chip={label}")
                path = OUT / f"web-{layout}-{theme}.png"
                await page.locator("main").screenshot(path=str(path), type="png")
                print(f"wrote {path}")
                await page.close()

        lcd = await context.new_page()
        await lcd.goto(base + "/lcd.html", wait_until="networkidle")
        for theme in THEMES:
            for layout in PAGES:
                eid = f"lcd-{layout}-{theme}"
                path = OUT / f"lcd-{layout}-{theme}.png"
                await lcd.locator(f"#{eid}").screenshot(path=str(path), type="png")
                print(f"wrote {path}")
        await browser.close()
    httpd.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
