#!/usr/bin/env python3
"""Rasterize K9DTV logos to PROGMEM RGB565 for MiniMe II LCD (dark + light).

Dark:  K9DTV/site/assets/k9dtv-logo.svg          -> K9DTV_LOGO_RGB565
Light: K9DTV/site/assets/k9dtv-logo-bright.svg   -> K9DTV_LOGO_BRIGHT_RGB565
Both share K9DTV_LOGO_W / K9DTV_LOGO_H (light padded/cropped to match dark).

Regenerate:
  python MinimeII/tools/gen_k9dtv_logo_rgb565.py
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

OUT_W = 233
OUT_H_HINT = 73  # target before content crop

ROOT = Path(__file__).resolve().parents[2]
SVG_DARK = ROOT / "K9DTV" / "site" / "assets" / "k9dtv-logo.svg"
SVG_BRIGHT = ROOT / "K9DTV" / "site" / "assets" / "k9dtv-logo-bright.svg"
SVG_DARK_H = ROOT / "MinimeII" / "MiniMe_Discord_Bot_II" / "k9dtv_logo_svg.h"
SVG_BRIGHT_H = ROOT / "MinimeII" / "MiniMe_Discord_Bot_II" / "k9dtv_logo_bright_svg.h"
DST = ROOT / "MinimeII" / "MiniMe_Discord_Bot_II" / "k9dtv_logo_rgb565.h"
TOOLS = ROOT / "MinimeII" / "tools"

BG_DARK = (0x12, 0x12, 0x12)
BG_LIGHT = (0xDD, 0xE2, 0xEA)  # LCD light --k9-space


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def load_svg(path: Path, header: Path) -> tuple[str, str]:
    if path.is_file():
        return path.read_text(encoding="utf-8"), str(path)
    text = header.read_text(encoding="utf-8")
    m = re.search(r'R"SVG\((.*)\)SVG"', text, re.S)
    if not m:
        raise SystemExit(f"no SVG in {header}")
    return m.group(1), str(header)


def tight_viewbox_svg(svg: str) -> str:
    svg_body = svg.strip()
    if svg_body.startswith("<?xml"):
        svg_body = svg_body.split("?>", 1)[-1].strip()
    vb = 'viewBox="144.5 57 318.5 93"'
    if 'viewBox="' in svg_body:
        svg_body = re.sub(r'viewBox="[^"]*"', vb, svg_body, count=1)
    else:
        svg_body = svg_body.replace("<svg ", f"<svg {vb} ", 1)
    svg_body = re.sub(r'\swidth="[^"]*"', f' width="{OUT_W}"', svg_body, count=1)
    svg_body = re.sub(r'\sheight="[^"]*"', f' height="{OUT_H_HINT}"', svg_body, count=1)
    return svg_body


async def rasterize(svg: str, bg: tuple[int, int, int], png_path: Path) -> None:
    from playwright.async_api import async_playwright

    svg_body = tight_viewbox_svg(svg)
    bg_css = f"rgb({bg[0]},{bg[1]},{bg[2]})"
    html = f"""<!DOCTYPE html><html><head><meta charset="utf-8">
<style>
html,body{{margin:0;padding:0;background:{bg_css};width:{OUT_W}px;height:{OUT_H_HINT}px;overflow:hidden}}
svg{{display:block;width:{OUT_W}px;height:{OUT_H_HINT}px}}
</style></head><body>{svg_body}</body></html>"""
    html_path = TOOLS / "_logo_render.html"
    html_path.write_text(html, encoding="utf-8")
    uri = html_path.resolve().as_uri()

    async with async_playwright() as p:
        browser = await p.chromium.launch()
        page = await browser.new_page(
            viewport={"width": OUT_W, "height": OUT_H_HINT},
            device_scale_factor=1,
        )
        await page.goto(uri, wait_until="networkidle")
        await page.screenshot(path=str(png_path), omit_background=False)
        await browser.close()


def content_bbox(img: Image.Image, bg: tuple[int, int, int], tol: int = 18):
    rgb = img.convert("RGB")
    w, h = rgb.size
    px = rgb.load()

    def is_bg(x: int, y: int) -> bool:
        r, g, b = px[x, y]
        return abs(r - bg[0]) <= tol and abs(g - bg[1]) <= tol and abs(b - bg[2]) <= tol

    top, left, right, bottom = h, w, 0, 0
    found = False
    for y in range(h):
        for x in range(w):
            if not is_bg(x, y):
                found = True
                top = min(top, y)
                bottom = max(bottom, y)
                left = min(left, x)
                right = max(right, x)
    if not found:
        return (0, 0, w, h)
    return (left, top, right + 1, bottom + 1)


def process_logo(img: Image.Image, bg: tuple[int, int, int]) -> Image.Image:
    img = img.convert("RGBA")
    base = Image.new("RGBA", img.size, (*bg, 255))
    img = Image.alpha_composite(base, img).convert("RGB")
    box = content_bbox(img, bg)
    img = img.crop(box)
    new_h = max(1, int(round(img.size[1] * (OUT_W / img.size[0]))))
    img = img.resize((OUT_W, new_h), Image.Resampling.LANCZOS)
    box2 = content_bbox(img, bg, tol=20)
    if box2[1] > 0:
        img = img.crop((0, box2[1], img.size[0], img.size[1]))
    return img


def match_size(img: Image.Image, w: int, h: int, bg: tuple[int, int, int]) -> Image.Image:
    """Pad or crop to exact w x h on bg."""
    if img.size == (w, h):
        return img
    out = Image.new("RGB", (w, h), bg)
    src = img
    if src.size[0] != w:
        nh = max(1, int(round(src.size[1] * (w / src.size[0]))))
        src = src.resize((w, nh), Image.Resampling.LANCZOS)
    if src.size[1] > h:
        # top-flush crop
        src = src.crop((0, 0, w, h))
        out.paste(src, (0, 0))
    else:
        out.paste(src, (0, 0))
    return out


def emit_array(name: str, img: Image.Image) -> list[str]:
    px = img.load()
    w, h = img.size
    lines = [f"static const uint16_t {name}[K9DTV_LOGO_W * K9DTV_LOGO_H] PROGMEM = {{"]
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


async def build_pair() -> None:
    dark_svg, dark_src = load_svg(SVG_DARK, SVG_DARK_H)
    bright_svg, bright_src = load_svg(SVG_BRIGHT, SVG_BRIGHT_H)

    png_d = TOOLS / "_k9dtv_logo_web.png"
    png_b = TOOLS / "_k9dtv_logo_web_bright.png"
    await rasterize(dark_svg, BG_DARK, png_d)
    await rasterize(bright_svg, BG_LIGHT, png_b)

    dark = process_logo(Image.open(png_d), BG_DARK)
    bright = process_logo(Image.open(png_b), BG_LIGHT)
    w, h = dark.size
    bright = match_size(bright, w, h, BG_LIGHT)

    dark.save(TOOLS / "_k9dtv_logo_lcd_preview.png")
    bright.save(TOOLS / "_k9dtv_logo_lcd_preview_bright.png")

    lines = [
        "// Auto-generated RGB565 LCD logos (dark + light). Do not edit by hand.",
        "// Regenerate: python MinimeII/tools/gen_k9dtv_logo_rgb565.py (Playwright Chromium + Pillow).",
        f"// Dark:  {dark_src}",
        f"// Light: {bright_src}",
        f"// Size: {w}x{h}",
        "#ifndef K9DTV_LOGO_RGB565_H",
        "#define K9DTV_LOGO_RGB565_H",
        "",
        "#include <Arduino.h>",
        "",
        f"#define K9DTV_LOGO_W {w}",
        f"#define K9DTV_LOGO_H {h}",
        "",
    ]
    lines += emit_array("K9DTV_LOGO_RGB565", dark)
    lines += [""]
    lines += emit_array("K9DTV_LOGO_BRIGHT_RGB565", bright)
    lines += ["", "#endif", ""]
    DST.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {DST} ({w}x{h}) dark+bright")


def main() -> None:
    asyncio.run(build_pair())


if __name__ == "__main__":
    main()
