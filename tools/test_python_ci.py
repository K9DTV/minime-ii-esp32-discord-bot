#!/usr/bin/env python3
"""pytest entry for host Python checks (stdlib sanity + Pillow logo helper)."""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import pytest

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))


def test_ci_sanity_passes() -> None:
    import ci_sanity

    assert ci_sanity.main() == 0


def test_logo_rgb565_helper() -> None:
    path = TOOLS / "gen_k9dtv_logo_rgb565.py"
    spec = importlib.util.spec_from_file_location("gen_k9dtv_logo_rgb565", path)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    assert mod.rgb565(255, 0, 0) == 0xF800
    assert mod.rgb565(0, 255, 0) == 0x07E0
    assert mod.rgb565(0, 0, 255) == 0x001F
    assert Path(mod.DST).name == "k9dtv_logo_rgb565.h"


def test_requirements_files_exist() -> None:
    assert (TOOLS / "requirements.txt").is_file()
    assert (TOOLS / "requirements-logo.txt").is_file()
    req = (TOOLS / "requirements.txt").read_text(encoding="utf-8")
    assert "pytest" in req
    assert "Pillow" in req
    assert "playwright" not in req  # logo-only extra
