#!/usr/bin/env python3
"""Host-side mirror of commands.cpp tokenize + CMD_CONSUMES_REST (spec for CI)."""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMANDS_CPP = ROOT / "MiniMe_Discord_Bot_II" / "commands.cpp"

# Must match kCmds[] CMD_CONSUMES_REST rows in commands.cpp.
CONSUMES_REST: frozenset[str] = frozenset({"!ask", "!display"})

# Known command names from kCmds (for "known vs unknown" checks).
KNOWN_CMDS: frozenset[str] = frozenset(
    {
        "!help",
        "!weather",
        "!news",
        "!physics",
        "!apod",
        "!iss",
        "!temp",
        "!sys",
        "!ota",
        "!coredump",
        "!time",
        "!ask",
        "!display",
        "!clear",
        "!resetprefs",
    }
)


@dataclass(frozen=True)
class TokenizeResult:
    ok: bool
    cmd: str
    args: str
    known: bool
    consumes_rest: bool


def strip_trailing_punct(s: str) -> str:
    while s and s[-1] in ".,!?;:":
        s = s[:-1]
    return s


def tokenize(content: str) -> TokenizeResult:
    """Mirror tokenizeCommand (commands.cpp). cmd lowercased; args keep case."""
    raw = content.strip()
    if not raw:
        return TokenizeResult(False, "", "", False, False)

    mid_line = False
    if not raw.startswith("!"):
        bang = -1
        for i, ch in enumerate(raw):
            if ch != "!":
                continue
            nxt = raw[i + 1] if i + 1 < len(raw) else ""
            if not (("a" <= nxt <= "z") or ("A" <= nxt <= "Z")):
                continue
            if i > 0 and raw[i - 1] not in " \t\n":
                continue
            bang = i
            break
        if bang < 0:
            return TokenizeResult(False, "", "", False, False)
        raw = raw[bang:]
        mid_line = True

    sp = raw.find(" ")
    if sp > 0:
        cmd = raw[:sp]
        args = raw[sp + 1 :]
    else:
        cmd = raw
        args = ""
    cmd = strip_trailing_punct(cmd.lower())
    args = args.strip()

    if len(cmd) <= 1:
        return TokenizeResult(False, "", "", False, False)

    known = cmd in KNOWN_CMDS
    consumes = cmd in CONSUMES_REST

    if mid_line and args and not consumes:
        asp = args.find(" ")
        if asp > 0:
            args = args[:asp]
        args = strip_trailing_punct(args)

    return TokenizeResult(True, cmd, args, known, consumes)


def _production_cpp_text(path: Path = COMMANDS_CPP) -> str:
    """Strip MINIME_TEST_TWDT blocks so CI matches production kCmds (no !hang)."""
    text = path.read_text(encoding="utf-8")
    return re.sub(
        r"#ifdef\s+MINIME_TEST_TWDT.*?\#endif",
        "",
        text,
        flags=re.DOTALL,
    )


def consumes_rest_from_cpp(path: Path = COMMANDS_CPP) -> set[str]:
    """Parse kCmds rows that include CMD_CONSUMES_REST."""
    text = _production_cpp_text(path)
    found: set[str] = set()
    for m in re.finditer(
        r'\{\s*"(![a-z]+)"\s*,\s*([^}]+)\}\s*,',
        text,
    ):
        name, flags = m.group(1), m.group(2)
        if "CMD_CONSUMES_REST" in flags:
            found.add(name)
    return found


def known_cmds_from_cpp(path: Path = COMMANDS_CPP) -> set[str]:
    text = _production_cpp_text(path)
    # Rows inside kCmds[] only -- names on lines with CMD_ flags.
    found: set[str] = set()
    in_table = False
    for line in text.splitlines():
        if "static const CmdEntry kCmds[]" in line:
            in_table = True
            continue
        if in_table and line.strip().startswith("};"):
            break
        if not in_table:
            continue
        m = re.search(r'"(![a-z]+)"', line)
        if m:
            found.add(m.group(1))
    return found
