#!/usr/bin/env python3
"""pytest: command tokenize / CMD_CONSUMES_REST (guards 0.4.9-class regressions)."""
from __future__ import annotations

import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from cmd_tokenize import (  # noqa: E402
    CONSUMES_REST,
    KNOWN_CMDS,
    consumes_rest_from_cpp,
    known_cmds_from_cpp,
    tokenize,
)


def test_cpp_consumes_rest_matches_python_spec() -> None:
    assert consumes_rest_from_cpp() == set(CONSUMES_REST)


def test_cpp_known_cmds_match_python_spec() -> None:
    assert known_cmds_from_cpp() == set(KNOWN_CMDS)


def test_cmd_word_lowercased_args_keep_case() -> None:
    # 0.4.9: lowercase the command word only; !display text keeps case.
    r = tokenize("!Display Hello World")
    assert r.ok and r.cmd == "!display"
    assert r.args == "Hello World"
    assert r.consumes_rest


def test_ask_keeps_multiword_args() -> None:
    r = tokenize("!ASK what is ESP32?")
    assert r.ok and r.cmd == "!ask"
    assert r.args == "what is ESP32?"
    assert r.consumes_rest


def test_weather_one_token_args() -> None:
    r = tokenize("!weather 90210 extra junk")
    assert r.ok and r.cmd == "!weather"
    # line-start: args are full rest (handler validates ZIP); mid-line truncates.
    assert r.args == "90210 extra junk"
    assert not r.consumes_rest


def test_midline_weather_truncates_to_one_word() -> None:
    r = tokenize("hey !weather 90210 please")
    assert r.ok and r.cmd == "!weather"
    assert r.args == "90210"
    assert not r.consumes_rest


def test_midline_ask_keeps_rest() -> None:
    r = tokenize("please !ask why is the sky blue today")
    assert r.ok and r.cmd == "!ask"
    assert r.args == "why is the sky blue today"
    assert r.consumes_rest


def test_midline_display_keeps_rest_case() -> None:
    r = tokenize("note: !display Keep CASE intact")
    assert r.ok and r.cmd == "!display"
    assert r.args == "Keep CASE intact"


def test_strip_punct_on_cmd_word() -> None:
    r = tokenize("!help.")
    assert r.ok and r.cmd == "!help"
    assert r.args == ""


def test_midline_strip_punct_on_short_arg() -> None:
    r = tokenize("go !clear.")
    assert r.ok and r.cmd == "!clear"
    assert r.args == ""


def test_unknown_bang_still_tokenizes() -> None:
    r = tokenize("!nope")
    assert r.ok and r.cmd == "!nope"
    assert not r.known


def test_plain_chat_without_bang_ignored() -> None:
    r = tokenize("hello there")
    assert not r.ok


def test_empty_ignored() -> None:
    assert not tokenize("").ok
    assert not tokenize("   ").ok
