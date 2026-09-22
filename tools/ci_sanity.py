#!/usr/bin/env python3
"""Host-side CI sanity for MiniMe II (no hardware). Exit 0 on pass."""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "MiniMe_Discord_Bot_II"
fails: list[str] = []


def fail(msg: str) -> None:
    fails.append(msg)


def main() -> int:
    ver_path = ROOT / "VERSION"
    if not ver_path.is_file():
        fail("VERSION file missing")
        ver = ""
    else:
        ver = ver_path.read_text(encoding="utf-8").strip()
        if not ver:
            fail("VERSION file empty")

    cfg_path = SKETCH / "minime_config.h"
    if not cfg_path.is_file():
        fail("minime_config.h missing")
    else:
        cfg = cfg_path.read_text(encoding="utf-8")
        m = re.search(r'#define\s+MINIME_VERSION\s+"([^"]+)"', cfg)
        if not m:
            fail("MINIME_VERSION not found in minime_config.h")
        elif ver and m.group(1) != ver:
            fail(f"VERSION ({ver}) != MINIME_VERSION ({m.group(1)})")

    inos = sorted(SKETCH.glob("*.ino"))
    if len(inos) != 1:
        fail(f"expected exactly one .ino in sketch folder, found {len(inos)}: {[p.name for p in inos]}")

    if not (SKETCH / "partitions.csv").is_file():
        fail("partitions.csv missing")

    ex = SKETCH / "secrets.example.h"
    if not ex.is_file():
        fail("secrets.example.h missing")
    else:
        ex_txt = ex.read_text(encoding="utf-8")
        if "MINIME_SECRETS_IS_EXAMPLE" not in ex_txt:
            fail("secrets.example.h must define MINIME_SECRETS_IS_EXAMPLE")

    gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8") if (ROOT / ".gitignore").is_file() else ""
    if "secrets.h" not in gitignore:
        fail(".gitignore must ignore secrets.h")

    # secrets.h may exist locally; must never be committed.
    try:
        tracked = subprocess.check_output(
            ["git", "ls-files", "--", "MiniMe_Discord_Bot_II/secrets.h"],
            cwd=ROOT,
            text=True,
        ).strip()
        if tracked:
            fail(f"secrets.h is tracked by git: {tracked}")
    except (OSError, subprocess.CalledProcessError):
        pass

    forbidden = ("StaticJsonDocument", "DynamicJsonDocument", "BasicJsonDocument")
    for path in list(SKETCH.glob("*.cpp")) + list(SKETCH.glob("*.h")) + list(SKETCH.glob("*.ino")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for tok in forbidden:
            if tok in text:
                fail(f"{path.name}: still contains ArduinoJson 6 type {tok}")

    wf = ROOT / ".github" / "workflows" / "compile.yml"
    if not wf.is_file():
        fail("compile.yml missing")
    else:
        wf_txt = wf.read_text(encoding="utf-8")
        if "ArduinoJson@7" not in wf_txt:
            fail("CI workflow must pin ArduinoJson 7.x")
        if "ci_sanity.py" not in wf_txt:
            fail("CI workflow must run tools/ci_sanity.py")

    if fails:
        print("ci_sanity FAILED:")
        for f in fails:
            print(f"  - {f}")
        return 1

    print(f"ci_sanity OK (VERSION={ver})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
