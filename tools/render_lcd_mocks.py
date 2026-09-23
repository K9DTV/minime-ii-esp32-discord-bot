#!/usr/bin/env python3
"""Render MiniMe II LCD mocks (480x320) to PNG for README.

Uses existing docs/lcd-mock/all-four.html + Playwright Chromium (same stack as gen_k9dtv_logo_rgb565.py).

  pip install -r MinimeII/tools/requirements-logo.txt
  playwright install chromium
  python MinimeII/tools/render_lcd_mocks.py
"""
from __future__ import annotations

import asyncio
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
MOCK = TOOLS.parent / "docs" / "lcd-mock"
HTML = MOCK / "all-four.html"

OUT = {
    "d-dark": MOCK / "display-dark.png",
    "d-light": MOCK / "display-light.png",
    "l-dark": MOCK / "log-dark.png",
    "l-light": MOCK / "log-light.png",
}


async def main() -> int:
    try:
        from playwright.async_api import async_playwright
    except ImportError:
        print("playwright required: pip install -r MinimeII/tools/requirements-logo.txt", file=sys.stderr)
        return 1

    if not HTML.is_file():
        print(f"missing {HTML}", file=sys.stderr)
        return 1

    uri = HTML.as_uri()
    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page(viewport={"width": 1100, "height": 1400}, device_scale_factor=2)
        await page.goto(uri, wait_until="networkidle")
        for eid, path in OUT.items():
            loc = page.locator(f"#{eid}")
            await loc.screenshot(path=str(path), type="png")
            print(f"wrote {path}")
        await browser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
