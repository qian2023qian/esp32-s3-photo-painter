#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成电量页 / 状态提示页中文字模 fontBatteryCN.c（24x25 点阵，格式对齐 font14CN.c）。

用法:
  python tools/gen_battery_font.py            # 生成 fontBatteryCN.c
  python tools/gen_battery_font.py --preview  # 额外生成预览 PNG（模拟电量页观感）

字模格式（见 components/port_bsp/src/fonts/fonts.h）:
  CH_CN { char index[3]; char matrix[...]; }
  实际为 24 宽 x 25 高，每行 24 位 = 3 字节（MSB first），共 75 字节/字。
  ASCII 字符同样存 24 宽点阵，绘制时 x 前进 ASCII_Width=14。
"""
import sys
sys.stdout.reconfigure(encoding="utf-8")
from PIL import Image, ImageFont, ImageDraw
from pathlib import Path
import numpy as np

FONT_PATH = "C:/Windows/Fonts/msyh.ttc"
FONT_SIZE = 23   # 所有汉字宽≤23 高≤23，无需缩放，避免笔画糊成黑块

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "components/port_bsp/src/fonts/fontBatteryCN.c"

# 电量页 + 状态提示页全部字符：汉字 35 个 + ASCII 16 个
# （提示页如“存储卡未检测到 / 请检查存储卡 / 未找到图片 / 图片读取失败”）
CHARS_CN = ["充", "电", "状", "态", "中", "阶", "段", "池", "压",
            "量", "恒", "流", "涓", "预", "已", "满", "未", "低", "请",
            # ---- 状态提示页新增 ----
            "存", "储", "卡", "检", "测", "到", "查", "找", "图", "片",
            "上", "传", "读", "取", "失", "败"]
CHARS_ASCII = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
               "%", "V", "m", ".", ":", " "]
ALL = CHARS_CN + CHARS_ASCII

W, H = 24, 25


def render_char(ch: str, is_ascii: bool = False) -> np.ndarray:
    """渲染单字符为 24x25 二值矩阵（True=黑）。

    - ASCII（is_ascii=True）：缩放到宽≤14（匹配 ASCII_Width），**左对齐**，
      避免 EPD_DrawStringCN 按 14px 前进时相邻字符重叠。
    - 汉字：不缩放，垂直/水平居中（字形宽≤23，自然留边）。
    """
    if ch == " ":
        return np.zeros((H, W), dtype=bool)
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    img = Image.new("L", (64, 64), 255)
    ImageDraw.Draw(img).text((2, 2), ch, font=font, fill=0)
    a = np.asarray(img)
    ys, xs = np.where(a < 128)
    if len(xs) == 0:
        return np.zeros((H, W), dtype=bool)
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    w, h = x1 - x0 + 1, y1 - y0 + 1
    glyph = a[y0:y1 + 1, x0:x1 + 1] < 128
    if w > W or h > H:   # 仅当超出 24x25 格子才缩放，避免笔画缩放糊成黑块
        scale = min(W / w, H / h)
        tw, th = max(1, int(w * scale)), max(1, int(h * scale))
        gimg = Image.fromarray((glyph * 255).astype(np.uint8)).resize((tw, th), Image.LANCZOS)
        glyph = np.asarray(gimg) < 128
        w, h = glyph.shape[1], glyph.shape[0]
    canvas = np.zeros((H, W), dtype=bool)
    ox = 0 if is_ascii else (W - w) // 2   # ASCII 左对齐，汉字居中
    oy = (H - h) // 2
    canvas[oy:oy + h, ox:ox + w] = glyph
    return canvas


def get_ascii_width() -> int:
    """所有 ASCII 字形的最大宽度，用作 cFONT 的 ASCII_Width，保证相邻字符不重叠。"""
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    mx = 0
    for ch in CHARS_ASCII:
        if ch == " ":
            continue
        img = Image.new("L", (64, 64), 255)
        ImageDraw.Draw(img).text((2, 2), ch, font=font, fill=0)
        a = np.asarray(img)
        ys, xs = np.where(a < 200)
        if len(xs):
            mx = max(mx, xs.max() - xs.min() + 1)
    return mx


def to_c_bytes(mat: np.ndarray) -> list:
    """24x25 矩阵 → 75 字节，每行 24 位打包 3 字节，MSB first。"""
    out = []
    for y in range(H):
        row = mat[y]
        for b in range(3):
            val = 0
            for bit in range(8):
                i = b * 8 + bit
                if i < W and row[i]:
                    val |= 0x80 >> bit
            out.append(val)
    return out


def fmt_c_file() -> str:
    lines = ['#include "fonts.h"', "",
             "// 电量页 / 状态提示页中文字库（由 tools/gen_battery_font.py 自动生成）",
             f"// 微软雅黑 {FONT_SIZE}px，24x25 点阵；ASCII 左对齐宽≤14，汉字居中", "",
             "const CH_CN FontBatteryCN_Table[] =", "{"]
    for ch in ALL:
        b = to_c_bytes(render_char(ch, ch in CHARS_ASCII))
        lines.append(f"/*--  文字:  {ch}  --*/")
        chunks = ["0x%02X" % v for v in b]
        # 每行最多 16 字节
        seg = []
        for i in range(0, len(chunks), 16):
            seg.append(", ".join(chunks[i:i + 16]))
        lines.append('{{"%s"},{%s}},' % (ch, ",\n".join(seg)))
        lines.append("")
    lines.append("};")
    lines += ["", "cFONT FontBatteryCN = {",
              "  FontBatteryCN_Table,",
              "  sizeof(FontBatteryCN_Table)/sizeof(CH_CN),  /* size of table */",
              f"  {get_ascii_width()}, /* ASCII Width (max glyph width) */",
              "  24, /* Width */",
              "  25, /* Height */",
              "};"]
    return "\n".join(lines)


def render_preview(out_png: Path) -> None:
    """按 EPD_DrawStringCN 布局渲染整页预览，检查观感。"""
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    page = Image.new("RGB", (800, 480), (255, 255, 255))
    d = ImageDraw.Draw(page)
    x0, y0 = 200, 150
    ascii_w = get_ascii_width()
    rows = ["充电状态：充电中", "充电阶段：恒压充电", "电池电压：4100mV", "电池电量：100%"]
    for ri, row in enumerate(rows):
        x, y = x0, y0 + ri * 40
        for ch in row:
            if ch == " ":
                x += ascii_w
                continue
            tw = ascii_w if ord(ch) <= 0xE0 else 24
            g = render_char(ch, ord(ch) <= 0xE0)
            for yy in range(H):
                for xx in range(W):
                    if g[yy, xx]:
                        page.putpixel((x + xx, y + yy), (0, 0, 0))
            x += tw
    page.save(out_png)
    print(f"预览已保存: {out_png}")


if __name__ == "__main__":
    src = fmt_c_file()
    OUT.write_text(src, encoding="utf-8")
    print(f"已生成 {OUT}")
    if "--preview" in sys.argv:
        render_preview(ROOT / "tools/battery_preview.png")
