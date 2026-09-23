#!/usr/bin/env python3
"""pytest: HTTP body reader with FakeClient (guards truncate / chunked false-success)."""
from __future__ import annotations

import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from http_body_reader import (  # noqa: E402
    MAX_BODY,
    FakeClient,
    encode_chunked,
    read_http_body_after_headers,
)


def test_content_length_ok() -> None:
    body = b'{"ok":true}'
    c = FakeClient(body)
    ok, out = read_http_body_after_headers(c, False, len(body))
    assert ok and out == body.decode()


def test_content_length_incomplete_is_false() -> None:
    # Class of bug: treating short body as success.
    c = FakeClient(b"partial", connected=False)
    ok, out = read_http_body_after_headers(c, False, 100)
    assert not ok
    assert out == "partial"


def test_chunked_ok() -> None:
    payload = encode_chunked([b"hello", b" world"])
    c = FakeClient(payload)
    ok, out = read_http_body_after_headers(c, True, -1)
    assert ok and out == "hello world"


def test_chunked_missing_final_zero_is_false() -> None:
    payload = encode_chunked([b"hello"], final=False)
    c = FakeClient(payload, connected=False)
    ok, _ = read_http_body_after_headers(c, True, -1)
    assert not ok


def test_chunked_truncated_mid_chunk_is_false() -> None:
    # Announce 10 bytes (hex "a"), deliver 4, then disconnect.
    payload = b"a\r\nabcd"
    c = FakeClient(payload, connected=False)
    ok, out = read_http_body_after_headers(c, True, -1)
    assert not ok
    assert out == "abcd"


def test_chunked_empty_body_final_zero_is_false() -> None:
    # Final 0-chunk with empty accumulation => false (firmware rule).
    c = FakeClient(b"0\r\n\r\n")
    ok, out = read_http_body_after_headers(c, True, -1)
    assert not ok
    assert out == ""


def test_chunked_hit_cap_is_false() -> None:
    big = b"x" * 100
    # max_body smaller than chunk so hitCap path returns false after trailer.
    payload = encode_chunked([big])
    c = FakeClient(payload)
    ok, _ = read_http_body_after_headers(c, True, -1, max_body=50)
    assert not ok


def test_content_length_over_max_body_truncated_false() -> None:
    data = b"y" * 80
    c = FakeClient(data)
    ok, _ = read_http_body_after_headers(c, False, 80, max_body=40)
    assert not ok


def test_until_close_ok() -> None:
    c = FakeClient(b"until-close-body", connected=False)
    ok, out = read_http_body_after_headers(c, False, -1)
    assert ok and out == "until-close-body"


def test_until_close_empty_false() -> None:
    c = FakeClient(b"", connected=False)
    ok, out = read_http_body_after_headers(c, False, -1)
    assert not ok and out == ""


def test_max_body_constant_matches_firmware() -> None:
    assert MAX_BODY == 48000
