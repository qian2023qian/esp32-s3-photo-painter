#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

from pathlib import Path
from flask import Flask, abort, send_file, Response, request, redirect, render_template_string
import mimetypes
import sqlite3
import json
import html
import os
import subprocess
import sys
import threading
import config as cfg
from io import BytesIO
import render_daily_photo as rdp
import settings_store

ROOT_DIR = Path(__file__).resolve().parent
WEBUI_DIR = ROOT_DIR / "webui"
LOGS_DIR = ROOT_DIR / "logs"
LOGS_DIR.mkdir(parents=True, exist_ok=True)
_RUNNING: dict[str, subprocess.Popen] = {}

# --- config ---
DB_PATH = Path(str(getattr(cfg, "DB_PATH", "./photos.db") or "./photos.db").strip().strip('"\'')).expanduser()
if not DB_PATH.is_absolute():
    DB_PATH = (ROOT_DIR / DB_PATH).resolve()

IMAGE_DIR = Path(str(getattr(cfg, "IMAGE_DIR", "") or "").strip().strip('"\'')).expanduser()
if not IMAGE_DIR.is_absolute():
    IMAGE_DIR = (ROOT_DIR / IMAGE_DIR).resolve()

OUTPUT_DIR = Path(str(getattr(cfg, "OUTPUT_DIR", "./output") or "./output")).expanduser()
if not OUTPUT_DIR.is_absolute():
    OUTPUT_DIR = (ROOT_DIR / OUTPUT_DIR).resolve()
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

FLASK_HOST = str(getattr(cfg, "FLASK_HOST", "0.0.0.0") or "0.0.0.0")
FLASK_PORT = int(getattr(cfg, "FLASK_PORT", 8765) or 8765)

# 是否开启照片库 WebUI（跑通后建议关闭，只保留 ESP32 下载接口）
ENABLE_REVIEW_WEBUI = bool(getattr(cfg, "ENABLE_REVIEW_WEBUI", True))

DAILY_PHOTO_QUANTITY = int(getattr(cfg, "DAILY_PHOTO_QUANTITY", 5) or 5)
if DAILY_PHOTO_QUANTITY < 1:
    DAILY_PHOTO_QUANTITY = 1


# review 分页：每页 100 张
REVIEW_PAGE_SIZE = 100

# /review 日期筛选的可用 MM-DD 列表缓存（避免每次都扫全库）
_MD_CACHE: dict[str, object] = {"md_list": [], "built_at": 0.0}
_MD_CACHE_TTL_SEC = 300.0  # 5 分钟

def _load_all_md_list() -> list[str]:
    """从全库提取所有存在的 MM-DD（去重、排序）。用于前端“随机一天”。"""
    if not DB_PATH.exists():
        return []

    # 简单 TTL 缓存
    import time
    now = time.time()
    try:
        built_at = float(_MD_CACHE.get("built_at") or 0.0)
    except Exception:
        built_at = 0.0
    if (now - built_at) < _MD_CACHE_TTL_SEC:
        cached = _MD_CACHE.get("md_list")
        if isinstance(cached, list):
            return [str(x) for x in cached]

    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    rows = c.execute("SELECT exif_json FROM photo_scores").fetchall()
    conn.close()

    s: set[str] = set()
    for (exif_json,) in rows:
        d = extract_date_from_exif(exif_json)
        if d and len(d) >= 10:
            md = d[5:10]
            if len(md) == 5 and md[2] == "-":
                s.add(md)

    md_list = sorted(s)
    _MD_CACHE["md_list"] = md_list
    _MD_CACHE["built_at"] = now
    return md_list

app = Flask(__name__)
def _require_webui_enabled() -> None:
    if not ENABLE_REVIEW_WEBUI:
        abort(404)


def _safe_join(base: Path, rel: str) -> Path:
    """防目录穿越：只允许 base 下的相对路径"""
    p = (base / rel).resolve()
    if not str(p).startswith(str(base.resolve())):
        raise ValueError("path traversal blocked")
    return p


def _send_static_file(p: Path) -> Response:
    if not p.exists() or not p.is_file():
        abort(404)

    if p.suffix.lower() == ".bin":
        return send_file(p, mimetype="application/octet-stream", as_attachment=False)

    mt, _ = mimetypes.guess_type(str(p))
    if mt:
        return send_file(p, mimetype=mt, as_attachment=False)
    return send_file(p, as_attachment=False)


def _make_image_url(path_str: str) -> str:
    """
    把数据库里的本地图片路径转换成 HTTP 可访问的 /images/... 路径。
    要求图片在 IMAGE_DIR 目录下；不在则返回空，避免 file:// 污染与 canvas 跨域。
    """
    try:
        p = Path(path_str).expanduser().resolve()
        rel = p.relative_to(IMAGE_DIR.resolve())
        return "/images/" + str(rel).replace("\\", "/")
    except Exception:
        return ""


# --------------------------
# DB helpers
# --------------------------


def load_rows(page: int = 1, page_size: int = REVIEW_PAGE_SIZE, md: str = "", sort: str = "memory"):
    """分页读取 review 数据。支持按 MM-DD 过滤与排序。返回 (rows, total_count)."""
    if not DB_PATH.exists():
        raise SystemExit(f"找不到数据库文件: {DB_PATH}")

    if page < 1:
        page = 1
    if page_size < 1:
        page_size = REVIEW_PAGE_SIZE

    offset = (page - 1) * page_size

    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    # 从 exif_json 里提取 datetime，再拼 MM-DD
    # 期望格式："YYYY:MM:DD HH:MM:SS"（extract_date_from_exif 也按这个假设）
    dt_expr = "json_extract(exif_json, '$.datetime')"
    md_expr = f"(substr({dt_expr}, 6, 2) || '-' || substr({dt_expr}, 9, 2))"

    where_sql = ""
    params: list[object] = []

    md = (md or "").strip()
    if md and len(md) == 5 and md[2] == "-":
        where_sql = f"WHERE {dt_expr} IS NOT NULL AND {md_expr} = ?"
        params.append(md)

    # total_count 也要跟随过滤
    if where_sql:
        total_count = c.execute(f"SELECT COUNT(1) FROM photo_scores {where_sql}", params).fetchone()[0]
    else:
        total_count = c.execute("SELECT COUNT(1) FROM photo_scores").fetchone()[0]

    # 排序
    sort = (sort or "memory").strip()
    if sort == "beauty":
        order_sql = "ORDER BY COALESCE(beauty_score, -1) DESC, COALESCE(memory_score, -1) DESC, path"
    elif sort == "time_new":
        # 直接按 datetime 字符串排序（固定格式下可按字典序比较）；NULL 放最后
        order_sql = f"ORDER BY ({dt_expr} IS NULL) ASC, {dt_expr} DESC, path"
    elif sort == "time_old":
        order_sql = f"ORDER BY ({dt_expr} IS NULL) ASC, {dt_expr} ASC, path"
    else:
        # 默认 memory
        order_sql = "ORDER BY COALESCE(memory_score, -1) DESC, COALESCE(beauty_score, -1) DESC, path"

    base_sql = f"""
        SELECT path,
               caption,
               type,
               memory_score,
               beauty_score,
               reason,
               exif_json,
               width,
               height,
               orientation,
               used_at,
               side_caption
        FROM photo_scores
        {where_sql}
        {order_sql}
        LIMIT ? OFFSET ?
    """

    q_params = list(params) + [page_size, offset]
    rows = c.execute(base_sql, q_params).fetchall()

    conn.close()
    return rows, int(total_count)


def load_sim_rows():
    if not DB_PATH.exists():
        raise SystemExit(f"找不到数据库文件: {DB_PATH}")

    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    rows = c.execute(
        """
        SELECT path,
               caption,
               type,
               memory_score,
               beauty_score,
               reason,
               side_caption,
               exif_json,
               width,
               height,
               orientation,
               used_at,
               exif_gps_lat,
               exif_gps_lon,
               exif_city
        FROM photo_scores
        """
    ).fetchall()

    conn.close()
    return rows


# 新增：只加载指定日期集合的照片，加速 /sim
def load_sim_rows_for_dates(dates: list[str]):
    """只加载指定日期（YYYY-MM-DD）集合内的照片，用于 /sim 加速。"""
    if not dates:
        return []
    if not DB_PATH.exists():
        raise SystemExit(f"找不到数据库文件: {DB_PATH}")

    # 过滤掉不合法日期字符串，避免 SQL 注入（虽然我们用参数化，但也别喂垃圾）
    safe_dates = []
    for d in dates:
        d = (d or "").strip()
        if len(d) == 10 and d[4] == "-" and d[7] == "-":
            safe_dates.append(d)
    if not safe_dates:
        return []

    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    dt_expr = "json_extract(exif_json, '$.datetime')"
    # exif datetime 形如 YYYY:MM:DD HH:MM:SS，取前 10 位并把 : 替换成 - -> YYYY-MM-DD
    date_expr = f"replace(substr({dt_expr}, 1, 10), ':', '-')"

    placeholders = ",".join(["?"] * len(safe_dates))
    sql = f"""
        SELECT path,
               caption,
               type,
               memory_score,
               beauty_score,
               reason,
               side_caption,
               exif_json,
               width,
               height,
               orientation,
               used_at,
               exif_gps_lat,
               exif_gps_lon,
               exif_city
        FROM photo_scores
        WHERE {dt_expr} IS NOT NULL
          AND {date_expr} IN ({placeholders})
    """

    rows = c.execute(sql, tuple(safe_dates)).fetchall()
    conn.close()
    return rows


def load_photo_full_row(abs_path: str):
    """按 path 查单张照片的完整行（字段与 load_sim_rows_for_dates 一致）。"""
    if not DB_PATH.exists():
        return None
    conn = sqlite3.connect(DB_PATH)
    row = conn.execute(
        """
        SELECT path, caption, type, memory_score, beauty_score, reason,
               side_caption, exif_json, width, height, orientation, used_at,
               exif_gps_lat, exif_gps_lon, exif_city
        FROM photo_scores
        WHERE path = ?
        LIMIT 1
        """,
        (abs_path,),
    ).fetchone()
    conn.close()
    return row


def get_photo_meta_by_path(abs_path: str):
    """
    从 DB 找到渲染需要的字段：date/side/lat/lon/city。
    abs_path 必须是数据库里 photo_scores.path 的原值（通常是绝对路径）。
    """
    if not DB_PATH.exists():
        return None

    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    row = c.execute(
        """
        SELECT path,
               exif_json,
               side_caption,
               memory_score,
               exif_gps_lat,
               exif_gps_lon,
               exif_city
        FROM photo_scores
        WHERE path = ?
        LIMIT 1
        """,
        (abs_path,),
    ).fetchone()
    conn.close()

    if not row:
        return None

    path, exif_json, side_caption, memory_score, gps_lat, gps_lon, exif_city = row
    date_str = extract_date_from_exif(exif_json)  # 可能为空（无 EXIF 拍摄时间）

    return {
        "path": str(path),
        "date": date_str,
        "side": side_caption or "",
        "memory": float(memory_score) if memory_score is not None else None,
        "lat": gps_lat,
        "lon": gps_lon,
        "city": exif_city or "",
    }

def summarize_exif(exif_json: str | None) -> str:
    if not exif_json:
        return ""

    try:
        data = json.loads(exif_json)
    except Exception:
        return ""

    dtv = data.get("datetime")
    make = data.get("make")
    model = data.get("model")
    iso = data.get("iso")
    exp = data.get("exposure_time")
    fnum = data.get("f_number")
    fl = data.get("focal_length")
    lat = data.get("gps_lat")
    lon = data.get("gps_lon")

    parts = []
    if dtv:
        parts.append(f"时间: {dtv}")
    if make or model:
        cam = f"{make or ''} {model or ''}".strip()
        if cam:
            parts.append(f"设备: {cam}")
    exp_parts = []
    if iso:
        exp_parts.append(f"ISO {iso}")
    if exp:
        exp_parts.append(f"快门 {exp}")
    if fnum:
        exp_parts.append(f"光圈 {fnum}")
    if fl:
        exp_parts.append(f"焦距 {fl}")
    if exp_parts:
        parts.append(" / ".join(exp_parts))
    if lat is not None and lon is not None:
        try:
            parts.append(f"GPS: {float(lat):.5f}, {float(lon):.5f}")
        except Exception:
            parts.append(f"GPS: {lat}, {lon}")

    return "；".join(str(p) for p in parts if p)


def extract_date_from_exif(exif_json: str | None) -> str:
    if not exif_json:
        return ""
    try:
        data = json.loads(exif_json)
    except Exception:
        return ""
    dtv = data.get("datetime")
    if not dtv:
        return ""
    try:
        date_part = str(dtv).split()[0]  # "2018:03:18"
        parts = date_part.replace(":", "-").split("-")
        if len(parts) >= 3:
            return f"{parts[0]}-{parts[1]}-{parts[2]}"
    except Exception:
        return ""
    return ""


# --------------------------
# HTML builders
# --------------------------

def build_html(rows, page: int, page_size: int, total_count: int):
    items_html = []

    for path, caption, ptype, m_score, b_score, reason, exif_json, width, height, orientation, used_at, side_caption in rows:
        safe_caption = html.escape(caption or "").replace("\n", "<br>")
        safe_side = html.escape(side_caption or "").replace("\n", "<br>")
        safe_type = html.escape(ptype or "")
        safe_reason = html.escape(reason or "")
        exif_summary = summarize_exif(exif_json)
        safe_exif = html.escape(exif_summary or "")

        date_str = extract_date_from_exif(exif_json)
        safe_date = html.escape(date_str or "")

        md_str = ""
        if date_str and len(date_str) >= 10:
            md_str = date_str[5:10]
        safe_md = html.escape(md_str or "")

        res_str = ""
        if width and height:
            try:
                res_str = f"{int(width)} x {int(height)}"
            except Exception:
                res_str = f"{width} x {height}"
        orient_str = orientation or ""
        used_str = used_at or ""

        img_uri = _make_image_url(str(path))
        if not img_uri:
            continue

        score_html = ""
        if m_score is not None or b_score is not None:
            parts = []
            if m_score is not None:
                parts.append(f"回忆度: {m_score:.1f}")
            if b_score is not None:
                parts.append(f"美观度: {b_score:.1f}")
            score_line = " / ".join(parts)
            score_html = f'<div class="score">{score_line}</div>'

        type_html = f'<div class="type">类型: {safe_type}</div>' if safe_type else ""
        exif_html = f'<div class="exif">{safe_exif}</div>' if safe_exif else ""
        reason_html = f'<div class="reason">理由: {safe_reason}</div>' if safe_reason else ""

        items_html.append(f"""
        <div class="item"
             data-date="{safe_date}"
             data-md="{safe_md}"
             data-memory="{m_score if m_score is not None else ''}"
             data-beauty="{b_score if b_score is not None else ''}">
            <div class="img-wrap">
                <a class="img-link" href="/sim?img={html.escape(img_uri)}" title="打开该照片的模拟器" onclick="window.stop();">
                    <img src="{img_uri}" loading="lazy">
                </a>
            </div>
            {f'<div class="side-under">{safe_side}</div>' if safe_side else ''}
            <div class="meta">
                <div class="path">{html.escape(str(path))}</div>
                {type_html}
                {score_html}
                {reason_html}
                {exif_html}
                <div class="extra">
                    {f"拍摄日期: {safe_date}" if safe_date else ""}
                    {(" · 分辨率: " + html.escape(res_str)) if res_str else ""}
                    {(" · 方向: " + html.escape(orient_str)) if orient_str else ""}
                    {(" · 已上屏: " + html.escape(used_str)) if used_str else ""}
                </div>
                <div class="caption">{safe_caption}</div>
            </div>
        </div>
        """)

    items_str = "\n".join(items_html)
    total_pages = (total_count + page_size - 1) // page_size

    # 从请求参数回填（用于显示）
    md_q = (request.args.get("md", "") or "").strip()
    sort_q = (request.args.get("sort", "") or "memory").strip() or "memory"
    md_hint = f" · 筛选日期 {html.escape(md_q)}" if (md_q and len(md_q) == 5) else ""

    html_str = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <title>InkTime照片数据库</title>
  <style>
    :root{{
      --bg: #0b0c10;
      --panel: rgba(255,255,255,0.06);
      --card: rgba(255,255,255,0.10);
      --card2: rgba(255,255,255,0.08);
      --text: rgba(255,255,255,0.92);
      --muted: rgba(255,255,255,0.62);
      --muted2: rgba(255,255,255,0.48);
      --line: rgba(255,255,255,0.14);
      --accent: #8ab4ff;
      --accent2:#9cffd6;
      --shadow: 0 18px 60px rgba(0,0,0,0.45);
      --shadow2: 0 10px 28px rgba(0,0,0,0.35);
      --radius: 14px;
    }}
    body{{
      margin:0;
      padding:0;
      font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", system-ui, sans-serif;
      background: radial-gradient(1200px 800px at 20% 0%, rgba(138,180,255,0.18), transparent 45%),
                  radial-gradient(900px 700px at 90% 20%, rgba(156,255,214,0.14), transparent 55%),
                  linear-gradient(180deg, #07080b 0%, #0b0c10 40%, #0b0c10 100%);
      color: var(--text);
    }}
    .container{{
      max-width: 1320px;
      margin: 26px auto 60px;
      padding: 0 18px;
    }}
    h1{{
      font-size: 22px;
      margin: 0 0 8px;
      letter-spacing: 0.2px;
    }}
    .subtitle{{
      font-size: 13px;
      color: var(--muted);
      margin: 0 0 14px;
      line-height: 1.35;
    }}

    .controls{{
      display:flex;
      flex-wrap:wrap;
      gap: 10px;
      align-items:center;
      margin: 12px 0 14px;
      font-size: 13px;
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: var(--radius);
      padding: 10px 12px;
      box-shadow: var(--shadow2);
      backdrop-filter: blur(10px);
    }}
    .controls label{{
      display:inline-flex;
      align-items:center;
      gap: 8px;
      color: var(--muted);
      white-space: nowrap;
    }}
    .controls select{{
      padding: 7px 10px;
      font-size: 13px;
      color: var(--text);
      background: rgba(255,255,255,0.08);
      border: 1px solid rgba(255,255,255,0.16);
      border-radius: 10px;
      outline: none;
    }}
    .controls select:focus{{
      border-color: rgba(138,180,255,0.7);
      box-shadow: 0 0 0 3px rgba(138,180,255,0.16);
    }}
    .controls button{{
      padding: 7px 12px;
      font-size: 13px;
      cursor: pointer;
      color: var(--text);
      background: rgba(255,255,255,0.10);
      border: 1px solid rgba(255,255,255,0.16);
      border-radius: 10px;
      transition: transform .08s ease, background .15s ease, border-color .15s ease, opacity .15s ease;
    }}
    .controls button:hover{{
      background: rgba(255,255,255,0.14);
      border-color: rgba(255,255,255,0.26);
    }}
    .controls button:active{{
      transform: translateY(1px);
    }}
    .controls button:disabled{{
      opacity: 0.45;
      cursor: not-allowed;
    }}
    .controls.pager{{
      background: rgba(255,255,255,0.05);
    }}

    .status{{
      font-size: 12px;
      color: var(--muted);
      margin: 8px 0 12px;
    }}

    .grid{{
      display:grid;
      grid-template-columns: repeat(auto-fill, minmax(300px, 1fr));
      gap: 16px;
    }}
    .item{{
      background: linear-gradient(180deg, var(--card) 0%, var(--card2) 100%);
      border: 1px solid rgba(255,255,255,0.14);
      border-radius: var(--radius);
      overflow: hidden;
      box-shadow: var(--shadow2);
      display:flex;
      flex-direction:column;
      transition: transform .12s ease, border-color .15s ease, box-shadow .15s ease;
    }}
    .item:hover{{
      transform: translateY(-2px);
      border-color: rgba(138,180,255,0.38);
      box-shadow: var(--shadow);
    }}

    .img-wrap{{
      width:100%;
      background: rgba(0,0,0,0.55);
      display:flex;
      align-items:center;
      justify-content:center;
      max-height: 260px;
      overflow:hidden;
    }}
    .img-wrap img{{
      width:100%;
      height:auto;
      display:block;
      object-fit: cover;
      filter: saturate(1.04) contrast(1.02);
    }}
    .img-link{{ display:block; width:100%; }}
    .img-link:link, .img-link:visited{{ text-decoration:none; }}

    .side-under{{
      padding: 10px 12px 0;
      font-size: 12px;
      color: var(--text);
      line-height: 1.45;
      word-break: break-word;
      opacity: 0.92;
    }}

    .meta{{
      padding: 10px 12px 12px;
      font-size: 13px;
      color: var(--text);
    }}
    .path{{
      font-size: 11px;
      color: var(--muted2);
      margin-bottom: 6px;
      word-break: break-all;
    }}
    .type{{
      font-size: 12px;
      color: var(--muted);
      margin-bottom: 4px;
    }}
    .score{{
      font-size: 13px;
      font-weight: 650;
      margin-bottom: 6px;
      color: var(--accent2);
    }}
    .reason{{
      font-size: 12px;
      color: var(--muted);
      margin-bottom: 6px;
      line-height: 1.45;
    }}
    .exif{{
      font-size: 11px;
      color: var(--muted2);
      margin-bottom: 8px;
      line-height: 1.45;
    }}
    .extra{{
      font-size: 11px;
      color: var(--muted2);
      margin-bottom: 8px;
      line-height: 1.45;
    }}
    .caption{{
      margin-top: 6px;
      font-size: 13px;
      line-height: 1.55;
      color: var(--text);
    }}

    @media (max-width: 560px){{
      .container{{ padding: 0 14px; }}
      .grid{{ grid-template-columns: 1fr; }}
      .controls{{ gap: 8px; }}
    }}
  </style>
</head>
<body>
  <div class="container">
    <h1>InkTime照片数据库</h1>
    <div class="subtitle">
      数据库：{html.escape(str(DB_PATH))}{md_hint} · 当前页 {page} · 本页 {len(rows)} 张 · 总计 {total_count} 张（每页 {page_size} 张）
    </div>

    <div class="controls">
      <label>
        月份：
        <select id="monthFilter">
          <option value="">全部</option>
          <option value="01">1 月</option><option value="02">2 月</option><option value="03">3 月</option>
          <option value="04">4 月</option><option value="05">5 月</option><option value="06">6 月</option>
          <option value="07">7 月</option><option value="08">8 月</option><option value="09">9 月</option>
          <option value="10">10 月</option><option value="11">11 月</option><option value="12">12 月</option>
        </select>
      </label>
      <label>
        日期：
        <select id="dayFilter">
          <option value="">全部</option>
          {''.join([f'<option value="{i:02d}">{i} 日</option>' for i in range(1, 32)])}
        </select>
      </label>
      <label>
        排序：
        <select id="sortBy">
          <option value="memory">按回忆度</option>
          <option value="beauty">按美观度</option>
          <option value="time_new">按时间（新→旧）</option>
          <option value="time_old">按时间（旧→新）</option>
        </select>
      </label>
      <button type="button" id="randomDateBtn">随机一天</button>
      <button type="button" id="homeBtn">回到首页</button>
    </div>

    <div class="controls pager" style="justify-content: space-between;">
      <div>
        <button type="button" id="prevPageBtn">上一页</button>
        <button type="button" id="nextPageBtn">下一页</button>
      </div>
      <div class="subtitle" style="margin:0;">第 <span id="pageNum">{page}</span> 页 / 共 <span id="pageTotal">{total_pages}</span> 页</div>
    </div>

    <div class="status" id="statusLine"></div>

    <div class="grid">
      {items_str}
    </div>

    <div class="controls pager" style="justify-content: space-between; margin-top: 18px;">
      <div>
        <button type="button" id="prevPageBtnBottom">上一页</button>
        <button type="button" id="nextPageBtnBottom">下一页</button>
      </div>
      <div class="subtitle" style="margin:0;">第 <span>{page}</span> 页 / 共 <span>{total_pages}</span> 页</div>
    </div>
  </div>

  <script>
    document.addEventListener('DOMContentLoaded', function () {{
      const monthSelect = document.getElementById('monthFilter');
      const daySelect = document.getElementById('dayFilter');
      const sortSelect = document.getElementById('sortBy');
      const statusLine = document.getElementById('statusLine');
      const randomBtn = document.getElementById('randomDateBtn');
      const homeBtn = document.getElementById('homeBtn');

      const currentPage = {page};
      const totalPages = {total_pages};
      const prevBtn = document.getElementById('prevPageBtn');
      const nextBtn = document.getElementById('nextPageBtn');
      const prevBtnBottom = document.getElementById('prevPageBtnBottom');
      const nextBtnBottom = document.getElementById('nextPageBtnBottom');

      // 任何跳转前先中断当前页面的图片/资源加载，避免请求排队导致“点击无响应”
      function navigateTo(urlStr) {{
        try {{
          window.stop();
        }} catch (e) {{
          // ignore
        }}
        window.location.href = urlStr;
      }}

      function getParams() {{
        const url = new URL(window.location.href);
        const md = (url.searchParams.get('md') || '').trim();
        const sort = (url.searchParams.get('sort') || '').trim() || 'memory';
        const page = parseInt(url.searchParams.get('page') || '1', 10) || 1;
        return {{ url, md, sort, page }};
      }}

      function setSelectsFromUrl() {{
        const p = getParams();
        // sort
        if (sortSelect) sortSelect.value = p.sort;
        // md -> month/day
        if (p.md && p.md.length === 5 && p.md.indexOf('-') === 2) {{
          const parts = p.md.split('-');
          if (parts.length === 2) {{
            if (monthSelect) monthSelect.value = parts[0];
            if (daySelect) daySelect.value = parts[1];
          }}
          if (statusLine) statusLine.textContent = '当前筛选：' + p.md + '（全库）';
        }} else {{
          if (monthSelect) monthSelect.value = '';
          if (daySelect) daySelect.value = '';
          if (statusLine) statusLine.textContent = '';
        }}
      }}

      function buildReviewUrl(md, sort, page) {{
        const url = new URL(window.location.href);
        url.pathname = '/review';
        if (md && md.length === 5 && md.indexOf('-') === 2) url.searchParams.set('md', md);
        else url.searchParams.delete('md');
        if (sort) url.searchParams.set('sort', sort);
        else url.searchParams.delete('sort');
        url.searchParams.set('page', String(page || 1));
        return url.toString();
      }}

      function goPage(p) {{
        const params = getParams();
        navigateTo(buildReviewUrl(params.md, params.sort, p));
      }}

      function goHome() {{
        const params = getParams();
        navigateTo(buildReviewUrl('', params.sort || 'memory', 1));
      }}

      async function pickRandomDate() {{
        // 从后端拿“真实存在的日期集合”，前端随机一个，然后让后端按 md 过滤
        try {{
          // 先停止当前页面的图片加载，释放连接
          try {{ window.stop(); }} catch (e) {{}}
          const resp = await fetch('/api/md_list');
          if (!resp.ok) throw new Error('HTTP ' + resp.status);
          const data = await resp.json();
          const arr = Array.isArray(data) ? data : (Array.isArray(data.md_list) ? data.md_list : []);
          if (!arr.length) {{
            if (statusLine) statusLine.textContent = '全库没有任何可用日期（exif datetime 缺失）。';
            return;
          }}
          const idx = Math.floor(Math.random() * arr.length);
          const md = String(arr[idx] || '').trim();
          const params = getParams();
          navigateTo(buildReviewUrl(md, params.sort || 'memory', 1));
        }} catch (e) {{
          if (statusLine) statusLine.textContent = '随机失败：' + e;
        }}
      }}

      function onMonthDayChange() {{
        const mVal = (monthSelect && monthSelect.value) ? monthSelect.value : '';
        const dVal = (daySelect && daySelect.value) ? daySelect.value : '';
        const sortBy = (sortSelect && sortSelect.value) ? sortSelect.value : 'memory';

        if (!mVal && !dVal) {{
          navigateTo(buildReviewUrl('', sortBy, 1));
          return;
        }}
        if (mVal && dVal) {{
          const md = mVal + '-' + dVal;
          navigateTo(buildReviewUrl(md, sortBy, 1));
          return;
        }}
        // 只选了一个，不跳转，避免生成无意义的 md
      }}

      function onSortChange() {{
        const params = getParams();
        const sortBy = (sortSelect && sortSelect.value) ? sortSelect.value : 'memory';
        navigateTo(buildReviewUrl(params.md, sortBy, 1));
      }}

      // 分页按钮
      if (prevBtn) {{
        prevBtn.disabled = currentPage <= 1;
        prevBtn.addEventListener('click', () => goPage(Math.max(1, currentPage - 1)));
      }}
      if (nextBtn) {{
        nextBtn.disabled = currentPage >= totalPages;
        nextBtn.addEventListener('click', () => goPage(Math.min(totalPages, currentPage + 1)));
      }}
      if (prevBtnBottom) {{
        prevBtnBottom.disabled = currentPage <= 1;
        prevBtnBottom.addEventListener('click', () => goPage(Math.max(1, currentPage - 1)));
      }}
      if (nextBtnBottom) {{
        nextBtnBottom.disabled = currentPage >= totalPages;
        nextBtnBottom.addEventListener('click', () => goPage(Math.min(totalPages, currentPage + 1)));
      }}

      if (monthSelect) monthSelect.addEventListener('change', onMonthDayChange);
      if (daySelect) daySelect.addEventListener('change', onMonthDayChange);
      if (sortSelect) sortSelect.addEventListener('change', onSortChange);
      if (randomBtn) randomBtn.addEventListener('click', pickRandomDate);
      if (homeBtn) homeBtn.addEventListener('click', goHome);

      // 兜底：用户在图片疯狂加载时点击任何链接/按钮，先 stop()，避免导航请求排队
      document.addEventListener('click', function (ev) {{
        const t = ev.target;
        if (!t) return;
        const a = t.closest ? t.closest('a') : null;
        const btn = t.closest ? t.closest('button') : null;
        // 只要是链接或按钮点击，就先中断当前加载
        if (a || btn) {{
          try {{ window.stop(); }} catch (e) {{}}
        }}
      }}, true);

      setSelectsFromUrl();
    }});
  </script>
</body>
</html>
"""
    return html_str


def build_simulator_html(sim_rows, selected_img: str = ""):
    """墨水屏渲染 + 调色 + 推送页（两套管线，替代原 InkTime 模拟器）。"""
    if not sim_rows:
        sim_rows = []
    info: dict = {}
    for row in sim_rows:
        try:
            (path, caption, ptype, memory_score, beauty_score, reason,
             side_caption, exif_json, width, height, orientation, used_at,
             gps_lat, gps_lon, exif_city) = row[:15]
        except Exception:
            continue
        img_uri = _make_image_url(str(path))
        if not img_uri or img_uri != selected_img:
            continue
        info = {
            "date": extract_date_from_exif(exif_json),
            "memory": float(memory_score) if memory_score is not None else None,
            "beauty": float(beauty_score) if beauty_score is not None else None,
            "side": side_caption or "",
            "caption": caption or "",
            "type": ptype or "",
            "reason": reason or "",
            "exif_summary": summarize_exif(exif_json) if exif_json else "",
            "width": width if width is not None else "",
            "height": height if height is not None else "",
            "orientation": orientation or "",
            "used_at": used_at or "",
            "lat": gps_lat,
            "lon": gps_lon,
            "city": exif_city or "",
        }
        break
    info_json = json.dumps(info, ensure_ascii=False).replace("</", "<\\/")
    selected_json = json.dumps(selected_img or "", ensure_ascii=False).replace("</", "<\\/")
    # 实时读 settings.json（settings_store 每次都读文件），避免 serve 进程内 config 模块缓存旧值
    cur_settings = settings_store.load()
    esp32_host = str(cur_settings.get("ESP32_HOST") or "192.168.4.1")
    saved_adj = dict(cur_settings.get("RENDER_ADJ") or {})
    render_adj_json = json.dumps(saved_adj, ensure_ascii=False).replace("</", "<\\/")
    render_dither = str(cur_settings.get("RENDER_DITHER") or "floydSteinberg")
    return render_template_string(
        _SIM_TEMPLATE,
        selected_json=selected_json, info_json=info_json, esp32_host=esp32_host,
        render_adj_json=render_adj_json, render_dither=render_dither,
    )


_SIM_TEMPLATE = """<!doctype html>
<html lang="zh"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>墨水屏渲染 · 调色 · 推送</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:#0d1117;color:#c9d1d9;padding:24px;max-width:1080px;margin:0 auto;font-size:18px;line-height:1.6}
h1{font-size:25px;margin-bottom:8px;color:#e6edf3}
.sub{font-size:14px;color:#8b949e;margin-bottom:18px}
.nav{display:flex;gap:12px;margin-bottom:20px;flex-wrap:wrap}
.nav a{background:#161b22;border:1px solid #30363d;color:#c9d1d9;text-decoration:none;padding:10px 22px;border-radius:10px;font-size:14px;transition:background .2s}
.nav a:hover{background:#1f2a3a}
.info-bar{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:16px 18px;font-size:14px;margin-bottom:14px}
.srow{display:flex;align-items:center;gap:12px;margin:7px 0;font-size:14px}
.skey{color:#8b949e;min-width:48px;flex-shrink:0}
.sbar{flex:1;height:14px;background:#21262d;border-radius:8px;overflow:hidden}
.sbar-fill{height:100%;border-radius:8px}
.sbar-fill.mem{background:linear-gradient(90deg,#6fd6ff,#9cffd6)}
.sbar-fill.bea{background:linear-gradient(90deg,#ffd36f,#ff9f6f)}
.sval{min-width:42px;text-align:right;color:#e6edf3}
.sval2{flex:1;color:#c9d1d9;word-break:break-all}
.bar{display:flex;gap:10px;align-items:center;padding:10px 14px;background:#161b22;border:1px solid #30363d;border-radius:10px;margin-bottom:14px}
.bar input{flex:1;padding:8px 10px;border-radius:8px;border:1px solid #30363d;background:#0d1117;color:#c9d1d9;font-size:14px;outline:none}
.cmp{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-bottom:16px}
.cmp .panel:first-child{grid-column:1/-1}
.panel{background:#161b22;border:1px solid #30363d;border-radius:14px;padding:14px}
.panel h2{font-size:15px;margin-bottom:12px;display:flex;justify-content:space-between;align-items:center;color:#e6edf3}
.tag{font-size:10px;padding:2px 8px;border-radius:5px}.tag-e{background:#8250df;color:#fff}.tag-o{background:#da3633;color:#fff}.tag-n{background:#8b949e;color:#fff}
.panel img{width:100%;max-height:480px;object-fit:contain;display:block;margin:0 auto;border-radius:10px;border:1px solid #21262d;background:#1a1a2e}
.btn-send{width:100%;margin-top:10px;padding:12px 0;border:none;border-radius:10px;cursor:pointer;font-size:15px;font-weight:600;color:#fff}
.btn-send:hover{filter:brightness(1.1)}
.msg{font-size:12px;margin-top:6px;text-align:center}.ok{color:#3fb950}.err{color:#f85149}
.sliders{background:#161b22;border:1px solid #30363d;border-radius:14px;padding:18px;display:flex;flex-direction:column;gap:12px;margin-bottom:16px}
.slider-item{display:flex;align-items:center;gap:10px;font-size:14px}
.slider-item .key{color:#8b949e;min-width:52px}
input[type=range]{-webkit-appearance:none;appearance:none;flex:1;height:24px;background:transparent;outline:none;cursor:pointer}
input[type=range]::-webkit-slider-runnable-track{height:8px;background:#30363d;border-radius:5px}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:26px;height:26px;background:#58a6ff;border-radius:50%;margin-top:-9px;cursor:pointer;border:3px solid #0d1117}
select{padding:8px 12px;border-radius:8px;border:1px solid #30363d;background:#0d1117;color:#c9d1d9;font-size:14px}
.slider-item .val{color:#8b949e;min-width:36px;text-align:right;font-size:14px}
.save-row{display:flex;align-items:center;gap:12px;margin-bottom:14px}
.btn-save{background:#238636;border:none;color:#fff;border-radius:10px;padding:10px 22px;cursor:pointer;font-size:14px}
.btn-save:hover{filter:brightness(1.1)}
.upload{border:2px dashed #30363d;border-radius:12px;padding:16px;text-align:center;cursor:pointer;font-size:14px;color:#8b949e;margin-bottom:14px}
.upload:hover{border-color:#58a6ff;background:#0d1117}
@media(max-width:800px){.cmp{grid-template-columns:1fr}}
</style></head><body>
<h1>墨水屏渲染 · 调色 · 推送</h1>
<p class="sub">两套管线（epdoptimize / OpenDisplay）渲染 6 色，调色后推送到相框（ai_&lt;时间戳&gt;.bmp）</p>
<div class="nav">
  <a href="/review">📷 照片库</a>
  <a href="/sim">🎨 渲染推送</a>
  <a href="/settings">⚙️ 设置</a>
</div>
<div class="bar">
  <span style="font-size:9px;color:#8b949e;white-space:nowrap">ESP32</span>
  <input id="esp32-host" value="{{ esp32_host }}">
</div>
<div class="info-bar" id="infoBar">加载中…</div>
<div class="cmp">
  <div class="panel">
    <h2><span>原图</span><span class="tag tag-n">原图</span></h2>
    <img id="preview-orig" alt="原图">
  </div>
  <div class="panel">
    <h2><span>epdoptimize</span><span class="tag tag-e">校准色板</span></h2>
    <canvas id="cb-src" style="display:none"></canvas>
    <canvas id="cb-cal" style="display:none"></canvas>
    <canvas id="cb-dev" style="display:none"></canvas>
    <img id="preview-epd" alt="epd">
    <button class="btn-send" style="background:#8250df" onclick="sendEpd()">推送到相框</button>
    <p class="msg" id="msg-epd"></p>
  </div>
  <div class="panel">
    <h2><span>OpenDisplay</span><span class="tag tag-o">纯色板</span></h2>
    <canvas id="co-src" style="display:none"></canvas>
    <canvas id="co-out" style="display:none"></canvas>
    <img id="preview-od" alt="od">
    <button class="btn-send" style="background:#da3633" onclick="sendOd()">推送到相框</button>
    <p class="msg" id="msg-od"></p>
  </div>
</div>
<div class="sliders">
  <div class="slider-item"><span class="key">亮度</span><input type="range" id="adj-br" min="-50" max="50" value="0" oninput="onAdj()"><span class="val" id="val-br">0</span></div>
  <div class="slider-item"><span class="key">对比度</span><input type="range" id="adj-ct" min="-50" max="50" value="20" oninput="onAdj()"><span class="val" id="val-ct">20</span></div>
  <div class="slider-item"><span class="key">饱和度</span><input type="range" id="adj-st" min="-50" max="50" value="20" oninput="onAdj()"><span class="val" id="val-st">20</span></div>
  <div class="slider-item"><span class="key">锐化</span><input type="range" id="adj-sh" min="0" max="100" value="50" oninput="onAdj()"><span class="val" id="val-sh">50</span></div>
  <div class="slider-item"><span class="key">扩散</span><input type="range" id="adj-df" min="0" max="200" value="100" oninput="onAdj()"><span class="val" id="val-df">100</span></div>
  <div class="slider-item"><span class="key">抖动</span><select id="adj-dither" onchange="onAdj()">
    <option value="floydSteinberg">Floyd-Steinberg</option>
    <option value="atkinson">Atkinson</option>
    <option value="jarvis">Jarvis-Judice-Ninke</option>
    <option value="stucki">Stucki</option>
    <option value="burkes">Burkes</option>
    <option value="sierra3">Sierra-3</option>
    <option value="sierra2">Sierra-2</option>
  </select></div>
</div>
<div class="save-row"><button class="btn-save" onclick="saveAdj()">💾 保存当前调色到设置</button><span class="msg" id="msg-save"></span></div>
<div class="upload" onclick="document.getElementById('file').click()">点击上传其他图片（JPG/PNG/BMP）</div>
<input type="file" id="file" accept="image/*" style="display:none" onchange="fileChanged(this)">
<script type="module">
import { ditherImage, replaceColors, aitjcizeSpectra6Palette } from '/lib/epdoptimize.js';
import { ditherImage as odDither, ColorScheme, DitherMode } from '/lib/opendisplay.js';

function $(id){return document.getElementById(id)}
const selectedImg = {{ selected_json|safe }};
const photoInfo = {{ info_json|safe }};
const esp32Default = "{{ esp32_host }}";
const savedAdj = {{ render_adj_json|safe }};
const savedDither = "{{ render_dither }}";
let srcCanvas=null, bmpEpd=null, bmpOd=null;
let W=480, H=800;

function buildBmpRaw(data,w,h){
  var rs=Math.ceil(3*w/4)*4, ps=rs*h, total=54+ps, bmp=new Uint8Array(total), dv=new DataView(bmp.buffer);
  dv.setUint8(0,0x42);dv.setUint8(1,0x4D);
  dv.setUint32(2,total,true);dv.setUint32(10,54,true);
  dv.setUint32(14,40,true);dv.setUint32(18,w,true);dv.setUint32(22,h,true);
  dv.setUint16(26,1,true);dv.setUint16(28,24,true);dv.setUint32(34,ps,true);
  var off=54;for(var y=h-1;y>=0;y--){var ro=0;
    for(var x=0;x<w;x++){var i=(y*w+x)*4;bmp[off+ro]=data[i+2];bmp[off+ro+1]=data[i+1];bmp[off+ro+2]=data[i];ro+=3}
    while(ro%4){bmp[off+ro]=0;ro++}off+=rs}
  return bmp;
}
function cl(v){return Math.max(0,Math.min(255,v))}
function getAdj(){
  return { br:Number($('adj-br').value), ct:Number($('adj-ct').value), st:Number($('adj-st').value),
           sh:Number($('adj-sh').value), df:Number($('adj-df').value), dither:$('adj-dither').value };
}
async function runEpd(){
  if(!srcCanvas)return;
  var a=getAdj();
  var sc=$('cb-src'), cal=$('cb-cal'), dev=$('cb-dev');
  sc.width=W;sc.height=H;sc.getContext('2d').drawImage(srcCanvas,0,0);
  cal.width=W;cal.height=H;dev.width=W;dev.height=H;
  await ditherImage(sc, cal, {
    palette: aitjcizeSpectra6Palette, errorDiffusionMatrix: a.dither, ditheringType:'errorDiffusion',
    serpentine:true, colorMatching:'lab',
    toneMapping:{mode:'contrast', exposure:a.br/100, saturation:a.st/50, contrast:a.ct/50, strength:a.df/100},
    clarity:{amount:a.sh/100, radius:1},
    dynamicRangeCompression:{mode:'auto', strength:0.5},
    processingEngine:'js', adjustmentEngine:'js'});
  replaceColors(cal, dev, aitjcizeSpectra6Palette);
  $('preview-epd').src=cal.toDataURL();
  bmpEpd=buildBmpRaw(dev.getContext('2d').getImageData(0,0,W,H).data, W, H);
}
function sharpen(d,w,h,s){
  if(s<=0)return d;var t=s/100,ctr=1+4*t,n=-t;
  var o=new Uint8ClampedArray(d.length);
  for(var y=0;y<h;y++)for(var x=0;x<w;x++){
    var i=(y*w+x)*4,r=d[i]*ctr,g=d[i+1]*ctr,b=d[i+2]*ctr;
    if(x>0){var j=i-4;r+=d[j]*n;g+=d[j+1]*n;b+=d[j+2]*n}
    if(x+1<w){var j=i+4;r+=d[j]*n;g+=d[j+1]*n;b+=d[j+2]*n}
    if(y>0){var j=i-w*4;r+=d[j]*n;g+=d[j+1]*n;b+=d[j+2]*n}
    if(y+1<h){var j=i+w*4;r+=d[j]*n;g+=d[j+1]*n;b+=d[j+2]*n}
    o[i]=cl(r);o[i+1]=cl(g);o[i+2]=cl(b);o[i+3]=d[i+3]}
  return o;
}
var pal=[[0,0,0],[255,255,255],[255,255,0],[255,0,0],null,[0,0,255],[0,255,0]];
var calCol=[[31,34,38],[185,199,201],[193,187,30],[98,32,30],null,[35,63,142],[53,86,58]];
var devCol=[[0,0,0],[255,255,255],[255,255,0],[255,0,0],null,[0,0,255],[0,255,0]];
function runOd(){
  if(!srcCanvas)return;
  var a=getAdj();
  var sc=$('co-src'), out=$('co-out');
  sc.width=W;sc.height=H;
  var ctx=sc.getContext('2d');ctx.drawImage(srcCanvas,0,0);
  var id=ctx.getImageData(0,0,W,H), d=id.data;
  var bf=1+a.br/100, cf=1+a.ct/100, sf=1+a.st/100;
  for(var y=0;y<H;y++)for(var x=0;x<W;x++){
    var i=(y*W+x)*4,r=d[i],g=d[i+1],b=d[i+2];
    r=cl(r*bf);g=cl(g*bf);b=cl(b*bf);
    r=cl((r-128)*cf+128);g=cl((g-128)*cf+128);b=cl((b-128)*cf+128);
    var gy=0.299*r+0.587*g+0.114*b;
    d[i]=cl(gy+(r-gy)*sf);d[i+1]=cl(gy+(g-gy)*sf);d[i+2]=cl(gy+(b-gy)*sf);
  }
  if(a.sh>0){id.data.set(sharpen(d,W,H,a.sh))}
  var modeName={floydSteinberg:'FLOYD_STEINBERG',atkinson:'ATKINSON',jarvis:'JARVIS_JUDICE_NINKE',stucki:'STUCKI',burkes:'BURKES',sierra3:'SIERRA',sierra2:'SIERRA_LITE'};
  var mode=DitherMode[modeName[a.dither]]||DitherMode.BURKES;
  var r=odDither({width:W,height:H,data:id.data}, ColorScheme.BWGBRY, {mode:mode, serpentine:true});
  out.width=W;out.height=H;
  var di=out.getContext('2d').createImageData(W,H);
  var prev=document.createElement('canvas');prev.width=W;prev.height=H;
  var pi=prev.getContext('2d').createImageData(W,H);
  for(var i=0;i<r.indices.length;i++){
    var c=r.palette[r.indices[i]], pr=c.r, pg=c.g, pb=c.b, idx=-1;
    for(var k=0;k<pal.length;k++){if(!pal[k])continue;
      if(pr===pal[k][0]&&pg===pal[k][1]&&pb===pal[k][2]){idx=k;break}}
    if(idx>=0&&calCol[idx]){pi.data[i*4]=calCol[idx][0];pi.data[i*4+1]=calCol[idx][1];pi.data[i*4+2]=calCol[idx][2]}
    else{pi.data[i*4]=pr;pi.data[i*4+1]=pg;pi.data[i*4+2]=pb}
    pi.data[i*4+3]=255;
    if(idx>=0&&devCol[idx]){di.data[i*4]=devCol[idx][0];di.data[i*4+1]=devCol[idx][1];di.data[i*4+2]=devCol[idx][2]}
    else{di.data[i*4]=pr;di.data[i*4+1]=pg;di.data[i*4+2]=pb}
    di.data[i*4+3]=255;
  }
  prev.getContext('2d').putImageData(pi,0,0);
  out.getContext('2d').putImageData(di,0,0);
  $('preview-od').src=prev.toDataURL();
  bmpOd=buildBmpRaw(di.data,W,H);
}
function processAndRender(img){
  // 自动适配比例：图高>宽用竖屏 480x800，否则横屏 800x480（与相框固件 applyScale 一致）
  const nw=img.naturalWidth, nh=img.naturalHeight;
  let tw=800, th=480;
  if(nh>nw){tw=480; th=800}
  W=tw; H=th;
  const c=document.createElement('canvas'); c.width=tw; c.height=th;
  const ctx=c.getContext('2d');
  ctx.fillStyle='#ffffff'; ctx.fillRect(0,0,tw,th);
  // 偏差 >= 25% 时等比留白，否则拉伸填满
  const dev=Math.abs(nw/nh - tw/th)/(tw/th);
  if(dev>=0.25){
    const s=Math.min(tw/nw, th/nh);
    const dw=nw*s, dh=nh*s;
    ctx.drawImage(img,(tw-dw)/2,(th-dh)/2,dw,dh);
  } else {
    ctx.drawImage(img,0,0,tw,th);
  }
  srcCanvas=c;
  $('preview-orig').src=c.toDataURL();
  onAdj();
}
function loadImg(url){return new Promise(function(res,rej){var im=new Image();im.onload=function(){res(im)};im.onerror=function(){rej(new Error('fail'))};im.src=url})}
function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;')}
function scoreBar(label,val,cls){
  const v=Math.max(0,Math.min(100,Number(val)||0));
  return '<div class="srow"><span class="skey">'+esc(label)+'</span><div class="sbar"><div class="sbar-fill '+cls+'" style="width:'+v+'%"></div></div><span class="sval">'+Math.round(v)+'</span></div>';
}
function renderInfo(){
  const ib=$('infoBar');
  if(!photoInfo||!Object.keys(photoInfo).length){ib.textContent='该照片暂无评分信息';return}
  let h='';
  if(photoInfo.memory!=null)h+=scoreBar('回忆分',photoInfo.memory,'mem');
  if(photoInfo.beauty!=null)h+=scoreBar('美观分',photoInfo.beauty,'bea');
  const rows=[];
  if(photoInfo.side)rows.push('<div class="srow"><span class="skey">文案</span><span class="sval2">'+esc(photoInfo.side)+'</span></div>');
  if(photoInfo.type)rows.push('<div class="srow"><span class="skey">类型</span><span class="sval2">'+esc(photoInfo.type)+'</span></div>');
  if(photoInfo.date)rows.push('<div class="srow"><span class="skey">日期</span><span class="sval2">'+esc(photoInfo.date)+'</span></div>');
  if(photoInfo.reason)rows.push('<div class="srow"><span class="skey">理由</span><span class="sval2">'+esc(photoInfo.reason)+'</span></div>');
  if(photoInfo.city)rows.push('<div class="srow"><span class="skey">地点</span><span class="sval2">'+esc(photoInfo.city)+'</span></div>');
  if(photoInfo.exif_summary)rows.push('<div class="srow"><span class="skey">EXIF</span><span class="sval2">'+esc(photoInfo.exif_summary)+'</span></div>');
  if(photoInfo.width&&photoInfo.height)rows.push('<div class="srow"><span class="skey">尺寸</span><span class="sval2">'+esc(photoInfo.width+'x'+photoInfo.height+(photoInfo.orientation?' ('+photoInfo.orientation+')':''))+'</span></div>');
  if(photoInfo.used_at)rows.push('<div class="srow"><span class="skey">使用</span><span class="sval2">'+esc(photoInfo.used_at)+'</span></div>');
  if(photoInfo.caption)rows.push('<div class="srow"><span class="skey">描述</span><span class="sval2">'+esc(photoInfo.caption)+'</span></div>');
  ib.innerHTML=h+rows.join('');
}
let adjTimer=null;
function onAdj(){
  $('val-br').textContent=$('adj-br').value;$('val-ct').textContent=$('adj-ct').value;
  $('val-st').textContent=$('adj-st').value;$('val-sh').textContent=$('adj-sh').value;
  $('val-df').textContent=$('adj-df').value;
  if(adjTimer)clearTimeout(adjTimer);
  adjTimer=setTimeout(function(){runEpd();runOd()},300);
}
function saveAdj(){
  var a=getAdj();
  var m=$('msg-save');m.textContent='保存中...';m.className='msg';
  fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({RENDER_ADJ:{br:a.br,ct:a.ct,st:a.st,sh:a.sh,df:a.df},RENDER_DITHER:a.dither})})
  .then(function(r){return r.json()}).then(function(d){
    m.textContent=d.ok?'已保存到设置（render 将用此调色）':'保存失败';m.className='msg '+(d.ok?'ok':'err');
  }).catch(function(){m.textContent='保存失败';m.className='msg err'});
}
function sendTo(msgId,bmp){
  if(!bmp){var m=$(msgId);m.textContent='请先选图';m.className='msg err';return}
  var host=($('esp32-host').value.trim()||esp32Default);host=host.replace('http://','').replace('https://','');
  var name='ai_'+Math.floor(Date.now()/1000)+'.bmp';
  var m=$(msgId);m.textContent='推送中...';m.className='msg';
  var x=new XMLHttpRequest();x.open('POST','http://'+host+'/api/upload?name='+encodeURIComponent(name));
  x.onload=function(){m.textContent='已推送 '+name+' ('+x.status+')';m.className='msg '+(x.status==200?'ok':'err')};
  x.onerror=function(){m.textContent='连接失败';m.className='msg err'};
  x.send(bmp);
}
function sendEpd(){sendTo('msg-epd',bmpEpd)}
function sendOd(){sendTo('msg-od',bmpOd)}
function fileChanged(input){var f=input.files[0];if(!f)return;var im=new Image();im.onload=function(){processAndRender(im)};im.src=URL.createObjectURL(f)}
function initAdj(){
  if(savedAdj){
    if(savedAdj.br!=null)$('adj-br').value=savedAdj.br;
    if(savedAdj.ct!=null)$('adj-ct').value=savedAdj.ct;
    if(savedAdj.st!=null)$('adj-st').value=savedAdj.st;
    if(savedAdj.sh!=null)$('adj-sh').value=savedAdj.sh;
    if(savedAdj.df!=null)$('adj-df').value=savedAdj.df;
  }
  if(savedDither)$('adj-dither').value=savedDither;
}
async function init(){
  renderInfo();
  initAdj();
  if(selectedImg){try{var im=await loadImg(selectedImg);processAndRender(im)}catch(e){}}
}
init();
window.sendEpd=sendEpd;window.sendOd=sendOd;window.fileChanged=fileChanged;window.onAdj=onAdj;window.saveAdj=saveAdj;
</script></body></html>"""


@app.get("/review")
def review():
    _require_webui_enabled()
    try:
        page = int(request.args.get("page", "1"))
    except Exception:
        page = 1

    md = (request.args.get('md', '') or '').strip()
    sort = (request.args.get('sort', '') or 'memory').strip() or 'memory'

    rows, total_count = load_rows(page=page, page_size=REVIEW_PAGE_SIZE, md=md, sort=sort)
    if not rows:
        return Response(
            "数据库里没有可展示的数据。请先运行你的分析脚本生成评分与文案。",
            status=404,
            mimetype="text/plain; charset=utf-8",
        )

    html_str = build_html(rows, page=page, page_size=REVIEW_PAGE_SIZE, total_count=total_count)
    return Response(html_str, mimetype="text/html; charset=utf-8")


@app.get('/api/md_list')
def api_md_list():
    _require_webui_enabled()
    md_list = _load_all_md_list()
    return Response(json.dumps(md_list, ensure_ascii=False), mimetype='application/json; charset=utf-8')


@app.get("/sim")
def sim():
    _require_webui_enabled()
    selected_img = request.args.get("img", "")

    # 默认不再全库加载，避免 /sim 页面巨大 JSON 导致浏览器转圈
    sim_rows = []

    # 仅当从 /review 点进来且参数合法时，按“该日期 + 向前 30 天”加载
    if selected_img and isinstance(selected_img, str) and selected_img.startswith("/images/"):
        subpath = selected_img[len("/images/"):]
        try:
            p = _safe_join(IMAGE_DIR, subpath)
        except Exception:
            p = None

        if p is not None and p.exists() and p.is_file():
            meta = get_photo_meta_by_path(str(p))
            base_date = meta.get("date") if meta else ""

            if base_date:
                try:
                    from datetime import datetime, timedelta
                    dt0 = datetime.strptime(base_date, "%Y-%m-%d")
                    dates = [(dt0 - timedelta(days=i)).strftime("%Y-%m-%d") for i in range(0, 31)]
                except Exception:
                    dates = [base_date]

                sim_rows = load_sim_rows_for_dates(dates)
            elif meta:
                # 无拍摄日期（如未装 exiftool）：至少加载当前照片信息供模拟器展示
                row = load_photo_full_row(str(p))
                sim_rows = [row] if row else []

    html_str = build_simulator_html(sim_rows, selected_img=selected_img)
    return Response(html_str, mimetype="text/html; charset=utf-8")


@app.get("/images/<path:subpath>")
def images(subpath: str):
    _require_webui_enabled()
    try:
        p = _safe_join(IMAGE_DIR, subpath)
    except Exception:
        abort(400)
    return _send_static_file(p)

@app.get("/sim_render")
def sim_render():
    _require_webui_enabled()

    img_uri = request.args.get("img", "")
    if not img_uri or not img_uri.startswith("/images/"):
        abort(400)

    subpath = img_uri[len("/images/"):]
    try:
        p = _safe_join(IMAGE_DIR, subpath)
    except Exception:
        abort(400)

    if not p.exists() or not p.is_file():
        abort(404)

    meta = get_photo_meta_by_path(str(p))
    if meta is None:
        # 兜底：DB 没命中就渲染纯图（不建议长期这样）
        meta = {
            "path": str(p),
            "date": "",
            "side": "",
            "memory": None,
            "lat": None,
            "lon": None,
            "city": "",
        }

    try:
        img = rdp.render_image(meta)
        from render_pipeline import render_image_6color

        res = render_image_6color(img, pipe="epd")
        bio = BytesIO()
        res["preview"].save(bio, format="PNG")
        bio.seek(0)
        return send_file(bio, mimetype="image/png", as_attachment=False)
    except Exception:
        abort(500)

@app.get("/lib/<path:name>")
def lib_asset(name: str):
    """提供 webui/lib/ 下的前端 bundle（epdoptimize.js / opendisplay.js）。"""
    if ".." in name or "/" in name:
        abort(400)
    p = WEBUI_DIR / "lib" / Path(name).name
    if not p.exists() or not p.is_file():
        abort(404)
    mt = "application/javascript" if p.suffix == ".js" else "application/octet-stream"
    return send_file(p, mimetype=mt, max_age=3600)


@app.get("/picker")
def picker():
    """交互式渲染 + 调色 + 推送页（两套管线）。"""
    p = WEBUI_DIR / "picker.html"
    if not p.exists():
        abort(404)
    return send_file(p, mimetype="text/html; charset=utf-8")


# ================= 配置管理 + 触发 analyze/render =================

@app.get("/api/settings")
def api_settings_get():
    data = settings_store.load()
    for ch in data.get("API_CHANNELS") or []:
        ch["has_key"] = bool(ch.get("api_key"))
        ch["api_key"] = ""  # 脱敏，不回显密钥
    return data


@app.post("/api/settings")
def api_settings_post():
    data = request.get_json(force=True, silent=True) or {}
    cur = settings_store.load()
    if "API_CHANNELS" in data and isinstance(data["API_CHANNELS"], list):
        old = {c.get("api_url", ""): c.get("api_key", "") for c in (cur.get("API_CHANNELS") or [])}
        for ch in data["API_CHANNELS"]:
            if not ch.get("api_key"):
                ch["api_key"] = old.get(ch.get("api_url", ""), "")
    settings_store.save(data)
    return {"ok": True}


def _spawn(name: str, args: list[str]) -> dict:
    if _RUNNING.get(name) and _RUNNING[name].poll() is None:
        return {"ok": False, "msg": f"{name} 正在运行中"}
    log_path = LOGS_DIR / f"{name}.log"
    env = dict(os.environ)
    env["PYTHONIOENCODING"] = "utf-8"
    p = subprocess.Popen(
        [sys.executable, "-u", "main.py"] + args, cwd=str(ROOT_DIR),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, encoding="utf-8", env=env, bufsize=1,
    )
    _RUNNING[name] = p

    def pump():
        """子进程输出：写入日志文件 + 实时打印到 serve 终端。"""
        try:
            with open(log_path, "w", encoding="utf-8") as f:
                for line in p.stdout:
                    f.write(line)
                    f.flush()
                    try:
                        print(f"[{name}] {line}", end="")
                        sys.stdout.flush()
                    except Exception:
                        pass
            p.stdout.close()
        except Exception:
            pass

    threading.Thread(target=pump, daemon=True).start()
    return {"ok": True, "pid": p.pid}


@app.post("/api/analyze")
def api_analyze():
    return _spawn("analyze", ["analyze"])


@app.post("/api/render")
def api_render():
    return _spawn("render", ["render"])


@app.post("/api/stop")
def api_stop():
    """停止正在运行的 analyze/render 子进程。"""
    stopped = []
    for name, p in _RUNNING.items():
        if p and p.poll() is None:
            try:
                p.terminate()
                stopped.append(name)
            except Exception:
                pass
    return {"ok": True, "stopped": stopped}


@app.get("/api/log/<name>")
def api_log(name: str):
    if name not in ("analyze", "render"):
        abort(400)
    p = LOGS_DIR / f"{name}.log"
    if not p.exists():
        return {"running": False, "content": ""}
    running = bool(_RUNNING.get(name)) and _RUNNING[name].poll() is None
    try:
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception:
        lines = []
    return {"running": running, "content": "\n".join(lines[-200:])}


@app.get("/settings")
def settings_page():
    return _SETTINGS_HTML


_SETTINGS_HTML = """<!doctype html>
<html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AI-Picker 设置</title>
<style>
*{box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:#0d1117;color:#c9d1d9;padding:24px;max-width:900px;margin:0 auto;font-size:18px;line-height:1.6}
h1{font-size:25px;color:#e6edf3;margin-bottom:6px}
fieldset{border:1px solid #30363d;border-radius:12px;padding:16px 18px;margin:14px 0}
legend{font-size:14px;color:#8b949e;padding:0 8px}
label{display:block;font-size:14px;margin:10px 0}
label span{display:inline-block;width:260px;color:#8b949e}
input[type=text],input[type=number],input[type=password],select{background:#0d1117;border:1px solid #30363d;color:#c9d1d9;border-radius:8px;padding:8px 10px;font-size:14px;width:340px}
input[type=text]:focus,input[type=number]:focus,input[type=password]:focus{border-color:#58a6ff;outline:none}
input[type=checkbox]{transform:scale(1.4)}
button{background:#238636;border:none;color:#fff;border-radius:8px;padding:10px 20px;cursor:pointer;font-size:15px}
button:hover{filter:brightness(1.1)}
button.sec{background:#1f6feb;margin-right:8px}button.orange{background:#bc4c00}button.red{background:#da3633}
.actions{margin:14px 0;display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.nav{display:flex;gap:12px;margin:14px 0 8px;flex-wrap:wrap}
.nav a{background:#161b22;border:1px solid #30363d;color:#c9d1d9;text-decoration:none;padding:10px 22px;border-radius:10px;font-size:14px}
.nav a:hover{background:#1f2a3a}
.ch-row{border:1px solid #21262d;border-radius:12px;padding:12px;margin:10px 0}
.ch-f{display:flex;align-items:center;margin:8px 0;gap:10px}
.ch-f span{display:inline-block;width:88px;color:#8b949e;font-size:14px;flex-shrink:0}
.ch-f input{flex:1;width:auto}
.ch-row button{background:#da3633;margin-top:8px}
pre{background:#010409;border:1px solid #30363d;border-radius:10px;padding:12px;font-size:13px;height:260px;overflow:auto;white-space:pre-wrap;color:#7ee787}
#status{font-size:14px;color:#8b949e}
</style></head><body>
<h1>AI-Photo-Picker 设置</h1>
<div class="nav">
  <a href="/review">📷 照片库 /review</a>
  <a href="/sim">🎨 渲染推送 /sim</a>
  <a href="/settings">⚙️ 设置 /settings</a>
</div>
<div class="actions">
  <button class="sec" onclick="runTask('analyze')">▶ 运行分析</button>
  <button class="orange" onclick="runTask('render')">▶ 渲染并推送</button>
  <button class="red" onclick="stopTask()">■ 停止任务</button>
  <span id="status"></span>
</div>
<pre id="log"></pre>
<form id="cfg" onsubmit="saveCfg(event)">
<fieldset><legend>相册 / 数据库</legend>
<label><span>相册目录</span><input name="IMAGE_DIR" type="text"></label>
<label><span>数据库路径</span><input name="DB_PATH" type="text"></label>
</fieldset>
<fieldset><legend>VLM 渠道</legend>
<div id="channels"></div>
<button type="button" onclick="addCh()" style="background:#30363d;color:#c9d1d9">+ 添加渠道</button>
<label><span>单批上限</span><input name="BATCH_LIMIT" type="text"></label>
<label><span>请求超时(秒)</span><input name="TIMEOUT" type="number"></label>
<label><span>渠道冷却(秒)</span><input name="CHANNEL_FAILOVER_COOLDOWN_SEC" type="number"></label>
<label><span>VLM 长边像素</span><input name="VLM_MAX_LONG_EDGE" type="number"></label>
</fieldset>
<fieldset><legend>ESP32 相框推送</legend>
<label><span>相框 IP</span><input name="ESP32_HOST" type="text"></label>
<label><span>端口</span><input name="ESP32_PORT" type="number"></label>
<label><span><input name="PUSH_ENABLED" type="checkbox"> 启用推送</span></label>
<label><span>文件前缀</span><input name="AI_NAME_PREFIX" type="text"></label>
<label><span>SD 卡上限</span><input name="AI_MAX_FILES" type="number"></label>
</fieldset>
<fieldset><legend>渲染方案</legend>
<label><span>管线</span><select name="RENDER_PIPELINE"><option value="epd">epdoptimize</option><option value="od">OpenDisplay</option></select></label>
<label><span>抖动算法</span><select name="RENDER_DITHER">
<option>floydSteinberg</option><option>atkinson</option><option>jarvis</option><option>stucki</option><option>burkes</option><option>sierra3</option><option>sierra2</option></select></label>
<label><span>亮度</span><input name="RENDER_ADJ.br" type="number"></label>
<label><span>对比度</span><input name="RENDER_ADJ.ct" type="number"></label>
<label><span>饱和度</span><input name="RENDER_ADJ.st" type="number"></label>
<label><span>锐化</span><input name="RENDER_ADJ.sh" type="number"></label>
<label><span>扩散</span><input name="RENDER_ADJ.df" type="number"></label>
<label><span>输出目录</span><input name="OUTPUT_DIR" type="text"></label>
<label><span>字体路径</span><input name="FONT_PATH" type="text"></label>
</fieldset>
<fieldset><legend>选片阈值</legend>
<label><span>回忆度阈值</span><input name="MEMORY_THRESHOLD" type="number" step="0.1"></label>
<label><span>每日数量</span><input name="DAILY_PHOTO_QUANTITY" type="number"></label>
</fieldset>
<fieldset><legend>WebUI</legend>
<label><span>监听地址</span><input name="FLASK_HOST" type="text"></label>
<label><span>端口</span><input name="FLASK_PORT" type="number"></label>
<label><span><input name="ENABLE_REVIEW_WEBUI" type="checkbox"> 启用 review WebUI</span></label>
</fieldset>
<button type="submit">保存配置</button>
</form>
<script>
const CH_KEYS = ['api_url','api_key','model_name'];
async function get(url){const r=await fetch(url);return r.json()}
async function post(url,body){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});return r.json()}

function addCh(ch){
  const d=document.createElement('div');d.className='ch-row';
  const kp=(ch&&ch.has_key)?'已保存(留空不修改)':'(可选)';
  d.innerHTML='<div class="ch-f"><span>接口地址</span><input class="url" placeholder="http://..."></div>'+
    '<div class="ch-f"><span>密钥</span><input class="k" type="password" placeholder="'+kp+'"></div>'+
    '<div class="ch-f"><span>模型名称</span><input class="mdl" placeholder="如 qwen3-vl-32b-instruct"></div>'+
    '<button type="button" onclick="this.parentNode.remove()">删除渠道</button>';
  if(ch){d.querySelector('.url').value=ch.api_url||'';d.querySelector('.k').value='';d.querySelector('.mdl').value=ch.model_name||''}
  document.getElementById('channels').appendChild(d);
}
function collectChannels(){
  return [...document.querySelectorAll('#channels .ch-row')].map(r=>({
    api_url:r.querySelector('.url').value.trim(),
    api_key:r.querySelector('.k').value.trim(),
    model_name:r.querySelector('.mdl').value.trim(),
  })).filter(c=>c.api_url);
}
async function loadCfg(){
  const d=await get('/api/settings');
  for(const [k,v] of Object.entries(d)){
    if(k==='API_CHANNELS'){(v||[]).forEach(addCh);continue}
    if(k==='RENDER_ADJ'){for(const [a,val] of Object.entries(v)){const el=document.querySelector('[name="RENDER_ADJ.'+a+'"]');if(el)el.value=val}continue}
    const el=document.querySelector('[name="'+k+'"]');
    if(!el)continue;
    if(el.type==='checkbox')el.checked=!!v;else el.value=(v===null||v===undefined)?'':v;
  }
}
async function saveCfg(e){
  e.preventDefault();
  const data={};
  new FormData(e.target).forEach((v,k)=>{data[k]=v});
  // RENDER_ADJ.* -> RENDER_ADJ dict
  const adj={};
  for(const k of Object.keys(data)){if(k.startsWith('RENDER_ADJ.')){adj[k.split('.')[1]]=Number(data[k])}}
  for(const k of Object.keys(data)){if(k.startsWith('RENDER_ADJ.'))delete data[k]}
  data.RENDER_ADJ=adj;
  data.API_CHANNELS=collectChannels();
  data.PUSH_ENABLED=document.querySelector('[name=PUSH_ENABLED]').checked;
  data.ENABLE_REVIEW_WEBUI=document.querySelector('[name=ENABLE_REVIEW_WEBUI]').checked;
  for(const k of ['BATCH_LIMIT','TIMEOUT','CHANNEL_FAILOVER_COOLDOWN_SEC','VLM_MAX_LONG_EDGE','ESP32_PORT','AI_MAX_FILES','MEMORY_THRESHOLD','DAILY_PHOTO_QUANTITY','FLASK_PORT']){
    if(data[k]!==undefined&&data[k]!=='')data[k]=Number(data[k]);
  }
  const r=await post('/api/settings',data);
  document.getElementById('status').textContent=r.ok?'已保存 ✓':'保存失败';
}
async function stopTask(){
  const r=await post('/api/stop',{});
  document.getElementById('status').textContent=r.stopped.length?('已停止: '+r.stopped.join(',')):'当前无运行任务';
  clearInterval(poll);
}
let poll=null;
async function runTask(name){
  const st=document.getElementById('status');st.textContent=name+' 启动中...';
  const r=await post('/api/'+name,{});
  st.textContent=r.ok?('运行中 (pid '+r.pid+')'):r.msg;
  clearInterval(poll);
  poll=setInterval(async()=>{
    const lg=await get('/api/log/'+name);
    document.getElementById('log').textContent=lg.content;
    if(!lg.running){clearInterval(poll);st.textContent=name+' 完成'}
  },1500);
}
loadCfg();
</script></body></html>"""


@app.get("/files/")
@app.get("/files/<path:subpath>")
def browse(subpath: str = ""):
    _require_webui_enabled()
    try:
        p = _safe_join(OUTPUT_DIR, subpath)
    except Exception:
        abort(400)

    if p.is_file():
        return _send_static_file(p)

    if not p.exists() or not p.is_dir():
        abort(404)

    items = []
    for child in sorted(p.iterdir(), key=lambda x: (not x.is_dir(), x.name.lower())):
        name = child.name + ("/" if child.is_dir() else "")
        rel = child.relative_to(OUTPUT_DIR)
        href = "/files/" + str(rel).replace("\\", "/")
        items.append(f'<li><a href="{html.escape(href)}">{html.escape(name)}</a></li>')

    up = ""
    if p != OUTPUT_DIR:
        parent_rel = p.parent.relative_to(OUTPUT_DIR)
        up_href = "/files/" + str(parent_rel).replace("\\", "/")
        up = f'<a href="{html.escape(up_href)}">⬅ 返回上级</a><br><br>'

    return f"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>InkTime Files</title>
<style>
body {{ font-family: -apple-system,BlinkMacSystemFont,system-ui,sans-serif; padding: 24px; }}
ul {{ line-height: 1.8; }}
code {{ background:#f2f2f2; padding:2px 6px; border-radius:4px; }}
</style>
</head>
<body>
<h3>输出目录浏览</h3>
<p>当前：<code>{html.escape(str(p.relative_to(OUTPUT_DIR) if p != OUTPUT_DIR else "."))}</code></p>
{up}
<ul>
{''.join(items)}
</ul>
</body>
</html>
"""


def main(host: str | None = None, port: int | None = None) -> None:
    """启动 Flask WebUI。host/port 可由 main.py serve 子命令覆盖。"""
    mimetypes.add_type("application/octet-stream", ".bin")
    h = host or FLASK_HOST
    p = port or FLASK_PORT
    print(f"[AI-Picker] DB: {DB_PATH}")
    print(f"[AI-Picker] IMAGE_DIR: {IMAGE_DIR}")
    print(f"[AI-Picker] OUT: {OUTPUT_DIR}")
    print(f"[AI-Picker] listen: {h}:{p}")
    print(f"[AI-Picker] open: http://127.0.0.1:{p}/  (本机)")
    app.run(host=h, port=p, debug=False)


if __name__ == "__main__":
    main()