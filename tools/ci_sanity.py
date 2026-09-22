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

    # Real secrets may exist locally; must never be committed anywhere in the tree.
    # secrets.example.h is the only allowed tracked secrets*.h template.
    try:
        listed = subprocess.check_output(
            ["git", "ls-files", "-z"],
            cwd=ROOT,
        )
        tracked_paths = [p.decode("utf-8", errors="replace") for p in listed.split(b"\0") if p]
        bad_secrets: list[str] = []
        for rel in tracked_paths:
            name = Path(rel).name
            if name == "secrets.example.h":
                continue
            if name == "secrets.h" or (
                name.startswith("secrets.") and name.endswith(".h")
            ) or (name.startswith("secrets") and name.endswith(".bak")):
                bad_secrets.append(rel)
        if bad_secrets:
            fail("secrets file(s) tracked by git (must not commit): " + ", ".join(bad_secrets))
    except (OSError, subprocess.CalledProcessError):
        pass

    forbidden = ("StaticJsonDocument", "DynamicJsonDocument", "BasicJsonDocument")
    for path in list(SKETCH.glob("*.cpp")) + list(SKETCH.glob("*.h")) + list(SKETCH.glob("*.ino")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for tok in forbidden:
            if tok in text:
                fail(f"{path.name}: still contains ArduinoJson 6 type {tok}")

    wf_sanity = ROOT / ".github" / "workflows" / "sanity.yml"
    wf_compile = ROOT / ".github" / "workflows" / "compile.yml"
    wf_python = ROOT / ".github" / "workflows" / "python.yml"
    wf_html = ROOT / ".github" / "workflows" / "html.yml"
    if not wf_sanity.is_file():
        fail("sanity.yml missing")
    else:
        st = wf_sanity.read_text(encoding="utf-8")
        if "ci_sanity.py" not in st:
            fail("sanity.yml must run tools/ci_sanity.py")
    if not wf_compile.is_file():
        fail("compile.yml missing")
    else:
        wf_txt = wf_compile.read_text(encoding="utf-8")
        if "ArduinoJson@7" not in wf_txt:
            fail("compile.yml must pin ArduinoJson 7.x")
    if not wf_python.is_file() or "pytest" not in wf_python.read_text(encoding="utf-8"):
        fail("python.yml missing or must run pytest")
    if not wf_html.is_file() or "ci_html.py" not in wf_html.read_text(encoding="utf-8"):
        fail("html.yml missing or must run ci_html.py")

    if fails:
        print("ci_sanity FAILED:")
        for f in fails:
            print(f"  - {f}")
        return 1

    print(f"ci_sanity OK (VERSION={ver})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
