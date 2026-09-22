#!/usr/bin/env python3
"""Check LAN web assets embedded in firmware headers (no board)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "MiniMe_Discord_Bot_II"
fails: list[str] = []


def fail(msg: str) -> None:
    fails.append(msg)


def extract_raw(text: str, marker: str) -> str | None:
    # R"CSS( ... )CSS" / R"JS( ... )JS" / R"SVG( ... )SVG"
    m = re.search(rf'R"{marker}\((.*)\){marker}"', text, re.S)
    return m.group(1) if m else None


def brace_balance(s: str, open_c: str, close_c: str) -> bool:
    n = 0
    for c in s:
        if c == open_c:
            n += 1
        elif c == close_c:
            n -= 1
            if n < 0:
                return False
    return n == 0


def main() -> int:
    assets = SKETCH / "web_assets.h"
    ui = SKETCH / "web_ui.cpp"
    if not assets.is_file():
        fail("web_assets.h missing")
        print_fails()
        return 1
    if not ui.is_file():
        fail("web_ui.cpp missing")

    at = assets.read_text(encoding="utf-8", errors="replace")
    css = extract_raw(at, "CSS")
    js_blocks = re.findall(r'R"JS\((.*?)\)JS"', at, re.S)
    if not css:
        fail("WEB_UI_CSS raw string missing")
    else:
        if not brace_balance(css, "{", "}"):
            fail("WEB_UI_CSS curly braces unbalanced")
        for needle in (
            "#box-metrics",
            "#box-users",
            "#box-logfile",
            "#box-serial",
            'html[data-theme="light"]',
            'html[data-layout="log"]',
            ".theme-chip-trigger",
        ):
            if needle not in css:
                fail(f"WEB_UI_CSS missing {needle}")

    if len(js_blocks) < 2:
        fail(f"expected WEB_UI_BOOT_JS + WEB_UI_JS (2 JS blocks), found {len(js_blocks)}")
    else:
        boot_js, app_js = js_blocks[0], js_blocks[1]
        if "k9-theme" not in boot_js:
            fail("boot JS missing k9-theme")
        if "mm-layout" not in boot_js:
            fail("boot JS missing mm-layout")
        for needle in ("THEME_KEY", "brand-logo", "theme-toggle", "layout-toggle", "/api/status"):
            if needle not in app_js:
                fail(f"WEB_UI_JS missing {needle}")
        if not brace_balance(app_js, "{", "}"):
            fail("WEB_UI_JS curly braces unbalanced")

    if ui.is_file():
        ut = ui.read_text(encoding="utf-8", errors="replace")
        for needle in (
            "box-metrics",
            "box-users",
            "box-logfile",
            "box-serial",
            "theme-toggle",
            "brand-logo",
            "WEB_UI_CSS",
            "WEB_UI_JS",
        ):
            if needle not in ut:
                fail(f"web_ui.cpp missing {needle}")

    for name in ("k9dtv_logo_svg.h", "k9dtv_logo_bright_svg.h", "menu_chip_svg.h"):
        p = SKETCH / name
        if not p.is_file():
            fail(f"{name} missing")
            continue
        t = p.read_text(encoding="utf-8", errors="replace")
        if 'R"SVG(' not in t or "<svg" not in t.lower():
            fail(f"{name}: expected R\"SVG(…)<svg…")

    if fails:
        print("ci_html FAILED:")
        for f in fails:
            print(f"  - {f}")
        return 1
    print("ci_html OK")
    return 0


def print_fails() -> None:
    print("ci_html FAILED:")
    for f in fails:
        print(f"  - {f}")


if __name__ == "__main__":
    sys.exit(main())
