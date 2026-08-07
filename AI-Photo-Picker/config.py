#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""运行时配置模块：从 settings.json 加载配置（由 WebUI 维护）。

各脚本用 `import config as cfg; getattr(cfg, "KEY", default)` 读取，
键缺失时抛 AttributeError，让 getattr 落到默认值，与原有用法完全兼容。
"""

from __future__ import annotations

import json
from pathlib import Path

_DATA: dict = {}
_SETTINGS_PATH = Path(__file__).resolve().parent / "settings.json"

if _SETTINGS_PATH.exists():
    try:
        loaded = json.loads(_SETTINGS_PATH.read_text(encoding="utf-8"))
        if isinstance(loaded, dict):
            _DATA = loaded
    except Exception:
        _DATA = {}


def __getattr__(name: str):
    if name in _DATA:
        return _DATA[name]
    raise AttributeError(name)
