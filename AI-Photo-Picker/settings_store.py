#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""配置存储：settings.json 的读写。WebUI 编辑配置，config.py 运行时加载。

键名使用与脚本 getattr(cfg, "KEY", default) 一致的大写常量名。
"""

from __future__ import annotations

import json
from pathlib import Path

SETTINGS_PATH = Path(__file__).resolve().parent / "settings.json"

# 全量配置项默认值（键名 = 脚本里 getattr(cfg, "KEY", default) 的 KEY）
DEFAULTS: dict = {
    # ---- 相册 / 数据库 ----
    "IMAGE_DIR": "./test",
    "DB_PATH": "./photos.db",
    # ---- VLM 渠道 ----
    "API_CHANNELS": [
        {
            "api_url": "http://127.0.0.1:1234/v1/chat/completions",
            "api_key": "",
            "model_name": "qwen3-vl-32b-instruct",
        }
    ],
    "BATCH_LIMIT": None,
    "TIMEOUT": 600,
    "CHANNEL_FAILOVER_COOLDOWN_SEC": 300,
    "VLM_MAX_LONG_EDGE": 2560,
    # ---- 城市 / GPS ----
    "WORLD_CITIES_CSV": "./data/world_cities_zh.csv",
    "CITY_GRID_DEG": 1.0,
    "CITY_MAX_DISTANCE_KM": 100.0,
    "HOME_LAT": 22.543096,
    "HOME_LON": 114.057865,
    "HOME_RADIUS_KM": 60.0,
    # ---- ESP32 相框推送 ----
    "ESP32_HOST": "192.168.4.1",
    "ESP32_PORT": 80,
    "PUSH_ENABLED": True,
    "AI_NAME_PREFIX": "ai_",
    "AI_MAX_FILES": 300,
    # ---- 渲染方案 ----
    "RENDER_PIPELINE": "epd",
    "RENDER_DITHER": "floydSteinberg",
    "RENDER_ADJ": {"br": 0, "ct": 20, "st": 20, "sh": 50, "df": 100},
    "OUTPUT_DIR": "./output",
    "FONT_PATH": "",
    # ---- 选片阈值 ----
    "MEMORY_THRESHOLD": 70.0,
    "DAILY_PHOTO_QUANTITY": 5,
    # ---- WebUI ----
    "FLASK_HOST": "0.0.0.0",
    "FLASK_PORT": 8765,
    "ENABLE_REVIEW_WEBUI": True,
    # ---- NAS（macOS，可选） ----
    "NAS_MOUNT_URL": "",
    "NAS_MOUNT_POINT": "/Volumes/photo",
    "NAS_RETRY_TIMES": 3,
    "NAS_RETRY_SLEEP_SEC": 2.0,
}


def load() -> dict:
    """读 settings.json，与默认值合并；首次不存在时生成默认文件。"""
    if not SETTINGS_PATH.exists():
        SETTINGS_PATH.write_text(
            json.dumps(DEFAULTS, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        return dict(DEFAULTS)
    try:
        data = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            data = {}
    except Exception:
        data = {}
    merged = {**DEFAULTS, **data}
    if not merged.get("API_CHANNELS"):
        merged["API_CHANNELS"] = DEFAULTS["API_CHANNELS"]
    return merged


def _clean(v):
    """递归剥离字符串首尾空白与引号（防止复制路径时带上引号）。"""
    if isinstance(v, str):
        return v.strip().strip('"\'')
    if isinstance(v, list):
        return [_clean(x) for x in v]
    if isinstance(v, dict):
        return {k: _clean(x) for k, x in v.items()}
    return v


def save(data: dict) -> None:
    """合并写入 settings.json（缺失键保留默认/旧值，字符串自动清理引号）。"""
    cur = load()
    cur.update({k: _clean(v) for k, v in data.items()})
    SETTINGS_PATH.write_text(
        json.dumps(cur, ensure_ascii=False, indent=2), encoding="utf-8"
    )
