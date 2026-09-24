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

# Full ASCII required (README/docs mojibake prevention).
ASCII_ONLY_SUFFIXES = frozenset(
    {
        ".md",
        ".txt",
        ".yml",
        ".yaml",
        ".html",
        ".css",
        ".csv",
        ".json",
        ".ps1",
        ".toml",
        ".ini",
        ".cfg",
    }
)
ASCII_ONLY_NAMES = frozenset({"VERSION", "LICENSE", "CHANGELOG", "README", "Dockerfile"})

# Source / tooling: ban punctuation that commonly mojibakes; allow Discord emoji etc.
MOJIBAKE_CODEPOINTS = frozenset(
    {
        0x00A0,  # nbsp
        0x00B7,  # middle dot (Display  -  v)
        0x00D7,  # multiply sign
        0x2011,  # non-breaking hyphen (Wi-Fi)
        0x2013,  # en dash
        0x2014,  # em dash
        0x2018,  # '
        0x2019,  # '
        0x201C,  # "
        0x201D,  # "
        0x2022,  # bullet
        0x2026,  # ...
        0x2192,  # ->
        0x2260,  # !=
        0x2264,  # <=
        0x2265,  # >=
    }
)
SOURCE_SUFFIXES = frozenset(
    {".h", ".hpp", ".c", ".cpp", ".ino", ".py", ".svg", ".js", ".cmake", ".example"}
)


def fail(msg: str) -> None:
    fails.append(msg)


def is_ascii_only_path(rel: str) -> bool:
    name = Path(rel).name
    if name in ASCII_ONLY_NAMES:
        return True
    return Path(rel).suffix.lower() in ASCII_ONLY_SUFFIXES


def is_source_path(rel: str) -> bool:
    name = Path(rel).name
    if name.endswith(".example") or name.endswith(".example.h"):
        return True
    return Path(rel).suffix.lower() in SOURCE_SUFFIXES


def describe_offenders(text: str, pred) -> list[str]:
    out: list[str] = []
    for i, ch in enumerate(text):
        if pred(ord(ch)):
            out.append(f"U+{ord(ch):04X}@{i}")
            if len(out) >= 5:
                break
    return out


def check_text_encoding(tracked_paths: list[str]) -> None:
    bad: list[str] = []
    for rel in tracked_paths:
        path = ROOT / rel
        if not path.is_file():
            continue
        ascii_only = is_ascii_only_path(rel)
        source = is_source_path(rel)
        if not ascii_only and not source:
            continue
        try:
            data = path.read_bytes()
        except OSError as e:
            fail(f"encoding check: cannot read {rel}: {e}")
            continue
        if not data:
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as e:
            bad.append(f"{rel}: not valid UTF-8 ({e})")
            continue
        if ascii_only:
            offenders = describe_offenders(text, lambda o: o > 127)
            if offenders:
                bad.append(f"{rel}: non-ASCII ({', '.join(offenders)})")
        else:
            offenders = describe_offenders(text, lambda o: o in MOJIBAKE_CODEPOINTS)
            if offenders:
                bad.append(f"{rel}: mojibake punctuation ({', '.join(offenders)})")
    if bad:
        show = bad[:40]
        more = len(bad) - len(show)
        msg = (
            "encoding: docs/config must be ASCII; "
            "source must not use em-dash/smart-quotes/arrows/etc.: "
            + "; ".join(show)
        )
        if more > 0:
            msg += f"; ...and {more} more"
        fail(msg)


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

    tracked_paths: list[str] = []
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

    if tracked_paths:
        check_text_encoding(tracked_paths)
        # Also scan untracked text under the tree (new docs before first commit).
        walk: list[str] = []
        for pat in (
            "**/*.md",
            "**/*.txt",
            "**/*.py",
            "**/*.yml",
            "**/*.yaml",
            "**/*.h",
            "**/*.cpp",
            "**/*.ino",
            "**/*.html",
            "**/*.css",
            "**/*.js",
            "**/*.svg",
            "**/*.ps1",
            "**/*.csv",
            "**/*.json",
        ):
            for p in ROOT.glob(pat):
                if ".git" in p.parts or "__pycache__" in p.parts or ".pytest_cache" in p.parts:
                    continue
                walk.append(str(p.relative_to(ROOT)).replace("\\", "/"))
        extra = [r for r in sorted(set(walk)) if r not in set(tracked_paths)]
        if extra:
            check_text_encoding(extra)
    else:
        walk: list[str] = []
        for pat in (
            "**/*.md",
            "**/*.txt",
            "**/*.py",
            "**/*.yml",
            "**/*.yaml",
            "**/*.h",
            "**/*.cpp",
            "**/*.ino",
            "**/*.html",
            "**/*.css",
            "**/*.js",
            "**/*.svg",
            "**/*.ps1",
            "**/*.csv",
            "**/*.json",
        ):
            for p in ROOT.glob(pat):
                if ".git" in p.parts or "__pycache__" in p.parts or ".pytest_cache" in p.parts:
                    continue
                walk.append(str(p.relative_to(ROOT)).replace("\\", "/"))
        check_text_encoding(sorted(set(walk)))

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
