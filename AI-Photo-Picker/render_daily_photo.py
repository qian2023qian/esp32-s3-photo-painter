#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
每日相册渲染脚本：
- 从 photos.db / photo_scores 中选出一张“历史上的今天”照片
- 按 InkTime 模拟器的布局渲染到 480x800
- 用 LXGWHeartSerifMN.ttf 把文案 / 日期 / 地点都画到图上
- 转成四色墨水屏（黑/白/红/黄）图像，并保存为 BIN（1 字节 1 像素，行优先）
- 同时导出 latest.h 头文件数组，给 ESP32 直接 include
"""

from __future__ import annotations

from pathlib import Path
import sqlite3
import json
import datetime as dt
import os
import sys
import time
from typing import List, Dict, Any, Tuple, Optional

# 强制 UTF-8 输出，避免重定向到日志文件时按 GBK 写导致 WebUI 乱码
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")
from PIL import Image, ImageDraw, ImageFont, ImageOps
import config as cfg


# --today 覆盖时由 main.py 注入
TODAY = getattr(cfg, "RENDER_TODAY", dt.date.today())

# === 路径配置（来自 config.py） ===
ROOT_DIR = Path(__file__).resolve().parent

DB_PATH = Path(str(getattr(cfg, "DB_PATH", "photos.db") or "photos.db")).expanduser()
if not DB_PATH.is_absolute():
    DB_PATH = (ROOT_DIR / DB_PATH).resolve()

OUTPUT_DIR = Path(str(getattr(cfg, "OUTPUT_DIR", "output") or "output")).expanduser()
if not OUTPUT_DIR.is_absolute():
    OUTPUT_DIR = (ROOT_DIR / OUTPUT_DIR).resolve()
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

FONT_PATH = Path(str(getattr(cfg, "FONT_PATH", "") or "")).expanduser()
if str(FONT_PATH) and not FONT_PATH.is_absolute():
    FONT_PATH = (ROOT_DIR / FONT_PATH).resolve()

MEMORY_THRESHOLD = float(getattr(cfg, "MEMORY_THRESHOLD", 70.0) or 70.0)


def _rank(it: Dict[str, Any]) -> float:
    """选片用的统一分数。

    真实照片看回忆度；网络图（表情包/梗图/二次元插画）按提示词设计**没有** memory_score，
    此时改用 有趣/深度/艺术 里的最高分 —— 否则整个梗图库会因为 memory 全是 -1
    而一张都选不出来（`> MEMORY_THRESHOLD` 对 -1 恒不成立）。
    """
    m = it.get("memory", -1.0)
    if m is not None and m >= 0:
        return float(m)
    vals = [float(it.get(k, -1.0)) for k in ("funny", "depth", "art")]
    vals = [v for v in vals if v is not None and v >= 0]
    return max(vals) if vals else -1.0
DAILY_PHOTO_QUANTITY = int(getattr(cfg, "DAILY_PHOTO_QUANTITY", 5) or 5)

# === 渲染方案与推送（来自 config.py） ===
RENDER_PIPELINE = str(getattr(cfg, "RENDER_PIPELINE", "epd") or "epd")
RENDER_DITHER = str(getattr(cfg, "RENDER_DITHER", "floydSteinberg") or "floydSteinberg")
RENDER_ADJ = dict(getattr(cfg, "RENDER_ADJ", {"br": 0, "ct": 20, "st": 20, "sh": 50, "df": 100}))
PUSH_ENABLED = bool(getattr(cfg, "PUSH_ENABLED", False))
ESP32_HOST = str(getattr(cfg, "ESP32_HOST", "") or "")
ESP32_PORT = int(getattr(cfg, "ESP32_PORT", 80) or 80)
AI_NAME_PREFIX = str(getattr(cfg, "AI_NAME_PREFIX", "ai_") or "ai_")

# 墨水屏尺寸
CANVAS_WIDTH = 480
CANVAS_HEIGHT = 800

# 底部文字区域高度
TEXT_AREA_HEIGHT = 100


# ========== DB 与 EXIF 处理 ==========

def extract_date_from_exif(exif_json: Optional[str]) -> str:
    """
    从 EXIF JSON 中提取拍摄日期，返回 YYYY-MM-DD 格式，失败则返回空字符串。
    逻辑与 review_web.py 中保持一致。
    """
    if not exif_json:
        return ""
    try:
        data = json.loads(exif_json)
    except Exception:
        return ""
    dt_str = data.get("datetime")
    if not dt_str:
        return ""
    try:
        date_part = str(dt_str).split()[0]
        parts = date_part.replace(":", "-").split("-")
        if len(parts) >= 3:
            return f"{parts[0]}-{parts[1]}-{parts[2]}"
    except Exception:
        return ""
    return ""


def load_sim_rows() -> List[Dict[str, Any]]:
    """
    加载 InkTime 用的核心字段：
    - path: 照片路径
    - exif_json: 用于解析日期 / GPS
    - side_caption: 文案
    - memory_score: 回忆度
    - exif_gps_lat / exif_gps_lon / exif_city: 地点信息（纯本地，不上网）
    """
    if not DB_PATH.exists():
        raise SystemExit(f"找不到数据库文件: {DB_PATH}")

    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    rows = c.execute(
        """
        SELECT path,
               exif_json,
               side_caption,
               memory_score,
               funny_score,
               depth_score,
               art_score,
               type,
               exif_gps_lat,
               exif_gps_lon,
               exif_city
        FROM photo_scores
        WHERE exif_json IS NOT NULL
        """
    ).fetchall()
    conn.close()

    items: List[Dict[str, Any]] = []
    for (path, exif_json, side_caption, memory_score, funny_score, depth_score, art_score,
         ptype, gps_lat, gps_lon, exif_city) in rows:
        date_str = extract_date_from_exif(exif_json)
        if not date_str:
            continue
        # 再次兜底过滤 Screenshot 等
        if "screenshot" in str(path).lower():
            continue

        try:
            y, m, d = map(int, date_str.split("-"))
        except Exception:
            continue
        md = f"{m:02d}-{d:02d}"

        item = {
            "path": str(path),
            "date": date_str,  # YYYY-MM-DD
            "md": md,          # MM-DD
            "side": side_caption or "",
            "memory": float(memory_score) if memory_score is not None else -1.0,
            "funny": float(funny_score) if funny_score is not None else -1.0,
            "depth": float(depth_score) if depth_score is not None else -1.0,
            "art": float(art_score) if art_score is not None else -1.0,
            "type": ptype or "",
            "lat": gps_lat,
            "lon": gps_lon,
            "city": exif_city or "",
        }
        items.append(item)

    return items


# ========== “历史上的今天”选片 ==========

def md_to_day_of_year(md: str) -> Optional[int]:
    """把 'MM-DD' 转成非闰年的第几天（1~365）。"""
    try:
        m, d = map(int, md.split("-"))
        days_before = [0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334]
        if m < 1 or m > 12:
            return None
        return days_before[m] + d
    except Exception:
        return None


def day_of_year_to_md(day: int) -> str:
    # 选一个非闰年（2001/2005 随便），只依赖 day-of-year。
    base = dt.date(2001, 1, 1) + dt.timedelta(days=day - 1)
    return f"{base.month:02d}-{base.day:02d}"


def choose_photo_for_today(items: List[Dict[str, Any]], today: dt.date) -> Tuple[Dict[str, Any], Dict[str, Any]]:
    """
    选片规则（按月日）：
    - 以 today 的月日为目标，例如 12 月 2 日 -> "12-02"
    - 在所有年份该月日的照片中，找 memory > MEMORY_THRESHOLD 的候选，随机选一张
    - 如果该月日没有任何 > 阈值的，则往前一天（月日）继续找（12-01, 11-30, ...），最多回溯 365 天
    - 如果整个 365 天都没有任何 > 阈值的照片，则在全局中选 memory 最大的一张作为兜底
    """

    if not items:
        raise RuntimeError("没有任何可用照片")

    # 按 md 分组
    by_md: Dict[str, List[Dict[str, Any]]] = {}
    for it in items:
        md = it["md"]
        by_md.setdefault(md, []).append(it)

    # 每组内按 memory 从高到低排序
    for arr in by_md.values():
        arr.sort(key=_rank, reverse=True)

    target_md = f"{today.month:02d}-{today.day:02d}"
    target_doy = md_to_day_of_year(target_md)
    if target_doy is None:
        raise RuntimeError(f"无法解析今天的月日: {target_md}")

    import random

    for offset in range(0, 365):
        doy = target_doy - offset
        if doy <= 0:
            doy += 365
        md = day_of_year_to_md(doy)

        arr = by_md.get(md, [])
        if not arr:
            continue
        candidates = [p for p in arr if _rank(p) > MEMORY_THRESHOLD]
        if not candidates:
            continue

        chosen = random.choice(candidates)
        info = {
            "target_md": target_md,
            "used_md": md,
            "day_offset": -offset,
            "candidate_count": len(candidates),
            "total_count_md": len(arr),
            "threshold": MEMORY_THRESHOLD,
            "fallback_global_max": False,
        }
        return chosen, info

    global_best = max(items, key=_rank)
    info = {
        "target_md": target_md,
        "used_md": global_best["md"],
        "day_offset": None,
        "candidate_count": 1,
        "total_count_md": len(by_md.get(global_best["md"], [])),
        "threshold": MEMORY_THRESHOLD,
        "fallback_global_max": True,
    }
    return global_best, info

def choose_photos_for_today(items: List[Dict[str, Any]], today: dt.date, count: int = 5) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
    """
    选片规则（多张版，按月日）：
    - 以 today 的月日为目标，例如 12 月 2 日 -> "12-02"
    - 在所有年份该月日的照片中，找 memory > MEMORY_THRESHOLD 的候选，尽量随机选 count 张
    - 如果该月日没有任何 > 阈值的，则往前一天（月日）继续找（12-01, 11-30, ...），最多回溯 365 天
    - 如果整个 365 天都没有任何 > 阈值的照片，则在全局中选回忆度最高的若干张作为兜底
    """
    if not items:
        raise RuntimeError("没有任何可用照片")

    # 按 md 分组
    by_md: Dict[str, List[Dict[str, Any]]] = {}
    for it in items:
        md = it["md"]
        by_md.setdefault(md, []).append(it)

    # 每组内按 memory 从高到低排序
    for arr in by_md.values():
        arr.sort(key=_rank, reverse=True)

    target_md = f"{today.month:02d}-{today.day:02d}"
    target_doy = md_to_day_of_year(target_md)
    if target_doy is None:
        raise RuntimeError(f"无法解析今天的月日: {target_md}")

    import random

    for offset in range(0, 365):
        doy = target_doy - offset
        if doy <= 0:
            doy += 365
        md = day_of_year_to_md(doy)

        arr = by_md.get(md, [])
        if not arr:
            continue
        candidates = [p for p in arr if _rank(p) > MEMORY_THRESHOLD]
        if not candidates:
            continue

        # 随机选不重复的多张
        if len(candidates) >= count:
            chosen_list = random.sample(candidates, count)
        else:
            # 候选不足 count 张，用该日剩余的高分照片补齐
            chosen_list = list(candidates)
            for extra in arr:
                if extra in chosen_list:
                    continue
                chosen_list.append(extra)
                if len(chosen_list) >= count:
                    break

        info = {
            "target_md": target_md,
            "used_md": md,
            "day_offset": -offset,
            "candidate_count": len(candidates),
            "total_count_md": len(arr),
            "threshold": MEMORY_THRESHOLD,
            "fallback_global_max": False,
        }
        return chosen_list, info

    # 兜底：全局回忆度最高的若干张
    sorted_all = sorted(items, key=_rank, reverse=True)
    chosen_list = sorted_all[:count]
    info = {
        "target_md": target_md,
        "used_md": chosen_list[0]["md"] if chosen_list else "",
        "day_offset": None,
        "candidate_count": len(chosen_list),
        "total_count_md": len(items),
        "threshold": MEMORY_THRESHOLD,
        "fallback_global_max": True,
    }
    return chosen_list, info
# ========== 绘制 ==========

def wrap_text_chinese(draw: ImageDraw.ImageDraw,
                      text: str,
                      font: ImageFont.FreeTypeFont,
                      max_width: int,
                      max_lines: int) -> List[str]:
    """
    简单中文按字符宽度折行。
    """
    if not text:
        return []
    lines: List[str] = []
    line = ""
    for ch in text:
        test = line + ch
        w = draw.textlength(test, font=font)
        if w <= max_width:
            line = test
        else:
            if line:
                lines.append(line)
            line = ch
            if len(lines) >= max_lines:
                break
    if line and len(lines) < max_lines:
        lines.append(line)
    return lines


def format_date_display(date_str: str) -> str:
    """
    "YYYY-MM-DD" -> "YYYY.M.D"
    """
    if not date_str:
        return ""
    parts = date_str.split("-")
    if len(parts) < 3:
        return date_str
    y = parts[0]
    try:
        m = str(int(parts[1]))
        d = str(int(parts[2]))
    except Exception:
        return date_str
    return f"{y}.{m}.{d}"


def format_location(lat, lon, city: str) -> str:
    """
    地点字符串：
    - 有 city 用 city
    - 否则如果有 lat/lon，用 "lat, lon"（5 位小数）
    - 否则空字符串（不写“未知地点”）
    """
    if city and str(city).strip():
        return str(city).strip()
    if lat is None or lon is None:
        return ""
    try:
        return f"{float(lat):.5f}, {float(lon):.5f}"
    except Exception:
        return ""


def render_image(item: Dict[str, Any]) -> Image.Image:
    """
    根据选中的 item 渲染一张 480x800 的 RGB 图像（竖屏）：
    - 上方图片：占 [0, CANVAS_HEIGHT - TEXT_AREA_HEIGHT)
    - 底部 TEXT_AREA_HEIGHT 像素为文字区：第一行 side 文案（最多两行），第二行日期 + 地点
    """
    canvas = Image.new("RGB", (CANVAS_WIDTH, CANVAS_HEIGHT), (255, 255, 255))
    draw = ImageDraw.Draw(canvas)

    # ---------- 加载原图并按 EXIF 方向纠正 ----------
    img_path = Path(item["path"])
    if not img_path.exists():
        raise RuntimeError(f"图片不存在: {img_path}")
    img = Image.open(img_path)
    img = ImageOps.exif_transpose(img).convert("RGB")

    img_w, img_h = img.size
    if img_w == 0 or img_h == 0:
        raise RuntimeError(f"图片尺寸非法: {img.size}")

    # ---------- 照片区域 ----------
    img_area_w = CANVAS_WIDTH
    img_area_h = CANVAS_HEIGHT - TEXT_AREA_HEIGHT  # 底部留给文字

    # “铺满裁剪”：缩放到至少覆盖区域，再从中间裁一块
    scale = max(img_area_w / img_w, img_area_h / img_h)
    draw_w = int(img_w * scale)
    draw_h = int(img_h * scale)

    img_resized = img.resize((draw_w, draw_h), Image.LANCZOS)

    left = max(0, (draw_w - img_area_w) // 2)
    top = max(0, (draw_h - img_area_h) // 2)
    right = left + img_area_w
    bottom = top + img_area_h
    img_cropped = img_resized.crop((left, top, right, bottom))

    # 贴到上方
    canvas.paste(img_cropped, (0, 0))

    # ---------- 底部文字区域 ----------
    padding_x = 24
    text_area_top = CANVAS_HEIGHT - TEXT_AREA_HEIGHT + 10
    text_width = CANVAS_WIDTH - 2 * padding_x

    try:
        font_big = ImageFont.truetype(str(FONT_PATH), 22)  # 文案
        font_small = ImageFont.truetype(str(FONT_PATH), 20)  # 日期/地点
    except Exception:
        font_big = ImageFont.load_default()
        font_small = ImageFont.load_default()

    side_text = item.get("side") or ""

    # 文案：最多两行，从 text_area_top 开始
    y = text_area_top
    if side_text:
        lines = wrap_text_chinese(draw, side_text, font_big, text_width, max_lines=2)
        for line in lines:
            draw.text((padding_x, y), line, font=font_big, fill=(0, 0, 0))
            y += 24  # 行高略大于字号

    # 日期 + 地点：固定在底部区域内的第二行
    date_display = format_date_display(item["date"])
    loc_display = format_location(item.get("lat"), item.get("lon"), item.get("city") or "")

    second_line_y = text_area_top + 54
    draw.text((padding_x, second_line_y), date_display, font=font_small, fill=(0, 0, 0))

    loc_w = draw.textlength(loc_display, font=font_small)
    loc_x = padding_x + text_width - loc_w
    if loc_x < padding_x:
        loc_x = padding_x
    draw.text((loc_x, second_line_y), loc_display, font=font_small, fill=(0, 0, 0))

    return canvas

# ========== 主流程 ==========

def main():
    items = load_sim_rows()
    if not items:
        raise SystemExit("没有可用照片（exif_json 为空或解析失败）。")

    photos, info = choose_photos_for_today(items, TODAY, count=DAILY_PHOTO_QUANTITY)

    print("[INFO] 目标月日:", info["target_md"])
    print("[INFO] 实际使用月日:", info["used_md"])
    print("[INFO] 回溯天数(day_offset):", info["day_offset"])
    print("[INFO] 候选数(>阈值):", info["candidate_count"])
    print("[INFO] 当日总数:", info["total_count_md"])
    print("[INFO] 使用兜底全局最大:", info["fallback_global_max"])

    if not photos:
        raise SystemExit("选片结果为空。")

    from render_pipeline import render_image_6color, push_to_esp32

    epoch = int(time.time())
    for idx, chosen in enumerate(photos):
        print(f"[INFO] 第 {idx} 张选中照片:", chosen["path"])
        print("[INFO] 拍摄日期:", chosen["date"])
        print("[INFO] 选片分:", round(_rank(chosen), 1), "  (回忆度:", chosen["memory"], ")")
        print("[DEBUG] 城市:", chosen.get("city", ""))
        print("[DEBUG] 经纬度:", chosen.get("lat"), chosen.get("lon"))
        print("[DEBUG] 文案:", chosen.get("side", ""))

        # 渲染成品图（照片 + 文案 + 日期 + 地点） -> 6 色 BMP
        img = render_image(chosen)
        res = render_image_6color(
            img,
            pipe=RENDER_PIPELINE,
            br=int(RENDER_ADJ.get("br", 0)),
            ct=int(RENDER_ADJ.get("ct", 20)),
            st=int(RENDER_ADJ.get("st", 20)),
            sh=int(RENDER_ADJ.get("sh", 50)),
            dither=RENDER_DITHER,
            df=int(RENDER_ADJ.get("df", 100)),
        )

        name = f"{AI_NAME_PREFIX}{epoch}_{idx}"
        bmp_path = OUTPUT_DIR / f"{name}.bmp"
        bmp_path.write_bytes(res["bmp"])
        preview_path = OUTPUT_DIR / f"preview_{idx}.png"
        res["preview"].save(preview_path)
        print(f"[OK] 已生成 {bmp_path} ({len(res['bmp'])}B) + {preview_path}")

        if PUSH_ENABLED and ESP32_HOST:
            push_result = push_to_esp32(res["bmp"], ESP32_HOST, ESP32_PORT, name=f"{name}.bmp")
            print(f"[PUSH] {push_result}")


if __name__ == "__main__":
    main()