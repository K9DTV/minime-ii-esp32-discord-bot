#!/usr/bin/env python3
"""Host-side mirror of readHttpBodyAfterHeaders (discord_http.cpp) for CI."""
from __future__ import annotations

from typing import Optional

HTTP_LINE_MAX = 512
MAX_BODY = 48000


class FakeClient:
    """Minimal Client stand-in: byte buffer + connected flag.

    Fully-buffered; deadline behavior is not modeled — only truncation semantics.
    """

    def __init__(self, data: bytes = b"", connected: bool = True) -> None:
        self._buf = bytearray(data)
        self._connected = connected
        self.stopped = False

    def available(self) -> int:
        return len(self._buf)

    def connected(self) -> bool:
        return self._connected and not self.stopped

    def read(self, size: Optional[int] = None) -> int | bytes:
        if size is None:
            if not self._buf:
                return -1
            return self._buf.pop(0)
        n = min(size, len(self._buf))
        if n <= 0:
            return b""
        chunk = bytes(self._buf[:n])
        del self._buf[:n]
        return chunk

    def stop(self) -> None:
        self.stopped = True
        self._connected = False
        self._buf.clear()

    def append(self, data: bytes) -> None:
        self._buf.extend(data)

    def disconnect(self) -> None:
        self._connected = False


def read_http_line_capped(client: FakeClient, deadline_ok: bool = True) -> Optional[str]:
    """Mirror readHttpLineCapped. deadline_ok=False forces timeout fail.

    FakeClient is fully-buffered; deadline behavior is not modeled — only truncation semantics.
    """
    if not deadline_ok:
        return None
    out = []
    while True:
        if client.available() == 0:
            if not client.connected() and client.available() == 0:
                return None
            # No data yet but still connected: unit tests feed all bytes up front.
            return None
        b = client.read()
        if isinstance(b, bytes):
            if not b:
                continue
            b = b[0]
        if b < 0:
            continue
        c = chr(b)
        if c == "\n":
            return "".join(out)
        if c == "\r":
            continue
        if len(out) < HTTP_LINE_MAX:
            out.append(c)


def read_http_body_after_headers(
    client: FakeClient,
    chunked: bool,
    content_length: int,
    *,
    max_body: int = MAX_BODY,
) -> tuple[bool, str]:
    """
    Mirror readHttpBodyAfterHeaders without millis/pumpNetWait.
    All body bytes must already be in FakeClient (or disconnect mid-stream).
    Returns (ok, body).
    """
    out = ""
    if chunked:
        empty_size_lines = 0
        while True:
            if client.available() == 0:
                if not client.connected():
                    client.stop()
                    return False, out
                # Waiting for more data with nothing pending = fail (no 0-chunk).
                client.stop()
                return False, out
            size_line = read_http_line_capped(client)
            if size_line is None:
                client.stop()
                return False, out
            if len(size_line) == 0:
                empty_size_lines += 1
                if empty_size_lines > 8:
                    client.stop()
                    return False, out
                continue
            empty_size_lines = 0
            sc = size_line.find(";")
            if sc >= 0:
                size_line = size_line[:sc]
            try:
                chunk_size = int(size_line.strip(), 16)
            except ValueError:
                client.stop()
                return False, out
            if chunk_size <= 0:
                return (len(out) > 0), out
            hit_cap = False
            got = 0
            while got < chunk_size:
                if client.available() > 0:
                    want = min(chunk_size - got, 256, client.available())
                    raw = client.read(want)
                    if not isinstance(raw, bytes) or len(raw) == 0:
                        if not client.connected():
                            client.stop()
                            return False, out
                        continue
                    got += len(raw)
                    if not hit_cap:
                        if len(out) + len(raw) > max_body:
                            hit_cap = True
                        else:
                            out += raw.decode("latin-1")
                elif not client.connected():
                    client.stop()
                    return False, out  # truncated mid-chunk
                else:
                    client.stop()
                    return False, out
            if got < chunk_size:
                client.stop()
                return False, out
            trailer = read_http_line_capped(client)
            if trailer is None:
                client.stop()
                return False, out
            if hit_cap:
                client.stop()
                return False, out
        # unreachable
    if content_length > 0:
        need = min(content_length, max_body)
        while len(out) < content_length:
            if client.available() > 0:
                remain = min(content_length - len(out), 256, client.available())
                raw = client.read(remain)
                if not isinstance(raw, bytes) or len(raw) == 0:
                    break
                if len(out) + len(raw) > max_body:
                    client.stop()
                    return False, out
                out += raw.decode("latin-1")
                if len(out) >= content_length:
                    break
            elif not client.connected() and client.available() == 0:
                break
            else:
                break
        if len(out) < content_length:
            client.stop()
            return False, out
        return True, out

    # Until-close fallback
    while True:
        if client.available() > 0:
            room = max_body - len(out)
            if room == 0:
                client.stop()
                return False, out
            want = min(room, 256, client.available())
            raw = client.read(want)
            if not isinstance(raw, bytes) or len(raw) == 0:
                break
            out += raw.decode("latin-1")
        elif not client.connected() and client.available() == 0:
            break
        else:
            break
    return (len(out) > 0), out


def encode_chunked(parts: list[bytes], final: bool = True) -> bytes:
    """Build a chunked body (size lines + data + CRLF), optional final 0-chunk."""
    out = bytearray()
    for p in parts:
        out.extend(f"{len(p):x}\r\n".encode("ascii"))
        out.extend(p)
        out.extend(b"\r\n")
    if final:
        out.extend(b"0\r\n\r\n")
    return bytes(out)
