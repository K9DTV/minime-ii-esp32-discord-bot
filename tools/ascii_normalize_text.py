#!/usr/bin/env python3
"""One-shot: normalize mojibake punctuation to ASCII in tracked text files."""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REPL = {
    "\u00a0": " ",
    "\u00b7": " - ",
    "\u00d7": "x",
    "\u00b0": " deg",
    "\u2011": "-",
    "\u2013": "-",
    "\u2014": "--",
    "\u2018": "'",
    "\u2019": "'",
    "\u201c": '"',
    "\u201d": '"',
    "\u2022": "-",
    "\u2026": "...",
    "\u2192": "->",
    "\u2260": "!=",
    "\u2264": "<=",
    "\u2265": ">=",
}

ASCII_SUFFIX = {
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
ASCII_NAMES = {"VERSION", "LICENSE", "CHANGELOG", "README", "Dockerfile"}
SOURCE_SUFFIX = {".h", ".hpp", ".c", ".cpp", ".ino", ".py", ".svg", ".js", ".cmake", ".example"}


def classify(rel: str) -> str | None:
    name = Path(rel).name
    if name in ASCII_NAMES or Path(rel).suffix.lower() in ASCII_SUFFIX:
        return "ascii"
    if name.endswith(".example") or name.endswith(".example.h"):
        return "source"
    if Path(rel).suffix.lower() in SOURCE_SUFFIX:
        return "source"
    return None


def main() -> int:
    try:
        listed = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
        paths = [p.decode("utf-8", "replace") for p in listed.split(b"\0") if p]
    except (OSError, subprocess.CalledProcessError):
        paths = []

    # Include untracked text under the tree (same idea as ci_sanity).
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
            rel = str(p.relative_to(ROOT)).replace("\\", "/")
            if rel not in paths:
                paths.append(rel)

    changed: list[str] = []
    for rel in paths:
        kind = classify(rel)
        if not kind:
            continue
        path = ROOT / rel
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        new = text
        for u, a in REPL.items():
            if u in new:
                new = new.replace(u, a)
        if kind == "ascii":
            new = "".join(ch if ord(ch) < 128 else "?" for ch in new)
        if new != text:
            # Keep existing newlines style roughly: write as UTF-8 LF
            path.write_bytes(new.encode("utf-8"))
            changed.append(rel)

    print(f"updated {len(changed)} files")
    for c in changed:
        print(f"  {c}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
