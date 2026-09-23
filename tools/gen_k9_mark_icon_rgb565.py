#!/usr/bin/env python3
"""Rasterize k9-mark-icon left/right (dark+bright) to PROGMEM RGB565 for MiniMe II LCD.

Sources: MiniMe_Discord_Bot_II/k9_mark_icon{,_bright,_right,_right_bright}_svg.h
  (dog nose left/right; K9 stays LTR at same place).

Regenerate:
  pip install -r MinimeII/tools/requirements.txt -r MinimeII/tools/requirements-logo.txt
  playwright install chromium
  python MinimeII/tools/gen_k9_mark_icon_rgb565.py
"""
from __future__ import annotations

import asyncio
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow required: pip install Pillow", file=sys.stderr)
    sys.exit(1)

OUT_W = 48
OUT_H = 32  # dog+K9 only (no button); matches ~118x78 viewBox

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / "MinimeII" / "MiniMe_Discord_Bot_II"
DST = SKETCH / "k9_mark_icon_rgb565.h"
TOOLS = ROOT / "MinimeII" / "tools"

SOURCES = [
    ("K9_MARK_LEFT_RGB565", SKETCH / "k9_mark_icon_svg.h", (0x12, 0x12, 0x12)),
    ("K9_MARK_LEFT_BRIGHT_RGB565", SKETCH / "k9_mark_icon_bright_svg.h", (0xE0, 0xF2, 0xF5)),
    ("K9_MARK_RIGHT_RGB565", SKETCH / "k9_mark_icon_right_svg.h", (0x12, 0x12, 0x12)),
    ("K9_MARK_RIGHT_BRIGHT_RGB565", SKETCH / "k9_mark_icon_right_bright_svg.h", (0xE0, 0xF2, 0xF5)),
]


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def load_svg(header: Path) -> str:
    text = header.read_text(encoding="utf-8")
    m = re.search(r'R"SVG\((.*)\)SVG"', text, re.S)
    if not m:
        raise SystemExit(f"no SVG in {header}")
    return m.group(1).strip()


async def rasterize(svg: str, bg: tuple[int, int, int], png_path: Path) -> None:
    from playwright.async_api import async_playwright

    svg_body = svg
    if svg_body.startswith("<?xml"):
        svg_body = svg_body.split("?>", 1)[-1].strip()
    svg_body = re.sub(r'\swidth="[^"]*"', f' width="{OUT_W}"', svg_body, count=1)
    svg_body = re.sub(r'\sheight="[^"]*"', f' height="{OUT_H}"', svg_body, count=1)
    bg_css = f"rgb({bg[0]},{bg[1]},{bg[2]})"
    html = f"""<!DOCTYPE html><html><head><meta charset="utf-8">
<style>
html,body{{margin:0;padding:0;background:{bg_css};width:{OUT_W}px;height:{OUT_H}px;overflow:hidden}}
svg{{display:block;width:{OUT_W}px;height:{OUT_H}px}}
</style></head><body>{svg_body}</body></html>"""
    html_path = TOOLS / "_mark_render.html"
    html_path.write_text(html, encoding="utf-8")
    uri = html_path.resolve().as_uri()

    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page(
            viewport={"width": OUT_W, "height": OUT_H},
            device_scale_factor=1,
        )
        await page.goto(uri, wait_until="networkidle")
        await page.screenshot(path=str(png_path), omit_background=False)
        await browser.close()


def emit_array(name: str, img: Image.Image) -> list[str]:
    px = img.load()
    w, h = img.size
    lines = [f"static const uint16_t {name}[K9_MARK_W * K9_MARK_H] PROGMEM = {{"]
    row: list[str] = []
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            row.append(f"0x{rgb565(r, g, b):04X}")
            if len(row) >= 12:
                lines.append("  " + ", ".join(row) + ",")
                row = []
    if row:
        lines.append("  " + ", ".join(row) + ",")
    lines.append("};")
    return lines


async def build() -> None:
    lines = [
        "// Auto-generated RGB565 LCD mark-icons (left/right, dark+bright). Do not edit by hand.",
        "// Regenerate: python MinimeII/tools/gen_k9_mark_icon_rgb565.py",
        "// Dog+K9 only (no button). Nose left/right; K9 LTR centered on dog body.",
        f"// Size: {OUT_W}x{OUT_H}",
        "#ifndef K9_MARK_ICON_RGB565_H",
        "#define K9_MARK_ICON_RGB565_H",
        "",
        "#include <Arduino.h>",
        "",
        f"#define K9_MARK_W {OUT_W}",
        f"#define K9_MARK_H {OUT_H}",
        "",
    ]

    for name, header, bg in SOURCES:
        svg = load_svg(header)
        png = TOOLS / f"_{name.lower()}.png"
        await rasterize(svg, bg, png)
        img = Image.open(png).convert("RGBA")
        base = Image.new("RGBA", img.size, (*bg, 255))
        img = Image.alpha_composite(base, img).convert("RGB")
        if img.size != (OUT_W, OUT_H):
            img = img.resize((OUT_W, OUT_H), Image.Resampling.LANCZOS)
        img.save(TOOLS / f"_{name.lower()}_preview.png")
        lines += emit_array(name, img)
        lines += [""]

    lines += ["#endif", ""]
    DST.write_text("\n".join(lines), encoding="utf-8")
    # Drop generator temps (do not leave build junk in tools/)
    for p in TOOLS.glob("_k9_mark_*.png"):
        p.unlink(missing_ok=True)
    html_tmp = TOOLS / "_mark_render.html"
    if html_tmp.is_file():
        html_tmp.unlink()
    print(f"wrote {DST} ({OUT_W}x{OUT_H} x4)")


def main() -> None:
    asyncio.run(build())


if __name__ == "__main__":
    main()
