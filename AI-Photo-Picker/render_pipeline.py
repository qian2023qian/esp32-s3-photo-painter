"""AI 相框 6 色渲染管线。

移植自相框仪表盘的两套渲染方案：
- epd 风格：epdoptimize（tinted 校准色板 + Lab 最近邻量化 + 误差扩散 + 转设备纯色）
- od  风格：OpenDisplay（前置 BCS 调色 + 纯色板加权 RGB 量化 + 误差扩散）

输出 24-bit BGR bottom-up BMP，ESP32 相框 /api/upload 可直接接收。
误差扩散矩阵精确转抄 epdoptimize_bundle.h。
"""

from __future__ import annotations

import struct
import sys

import numpy as np

# ---------------- 调色板常量 ----------------

# epdoptimize aitjcize-spectra6 校准色（顺序：黑/白/蓝/绿/红/黄）
EPD_CAL_PALETTE = [
    (0x02, 0x02, 0x02), (0xBE, 0xC8, 0xC8), (0x05, 0x40, 0x9E),
    (0x27, 0x66, 0x3C), (0x87, 0x13, 0x00), (0xCD, 0xCA, 0x00),
]
# epdoptimize deviceColor 设备纯色（与 EPD_CAL_PALETTE 同索引顺序）
EPD_DEV_PALETTE = [
    (0, 0, 0), (255, 255, 255), (0, 0, 255), (0, 255, 0), (255, 0, 0), (255, 255, 0),
]
# OpenDisplay BWGBRY 纯色（顺序：黑/白/黄/红/蓝/绿）
OD_PALETTE = [
    (0, 0, 0), (255, 255, 255), (255, 255, 0), (255, 0, 0), (0, 0, 255), (0, 255, 0),
]
# 纯色(黑/白/黄/红/蓝/绿) -> 墨水屏校准色（预览用）
DEV_TO_CAL = {
    (0, 0, 0): (31, 34, 38), (255, 255, 255): (185, 199, 201),
    (255, 255, 0): (193, 187, 30), (255, 0, 0): (98, 32, 30),
    (0, 0, 255): (35, 63, 142), (0, 255, 0): (53, 86, 58),
}

# ---------------- 误差扩散矩阵 ----------------

ERROR_DIFFUSION_MATRICES = {
    "floydSteinberg": [(1, 0, 7 / 16), (-1, 1, 3 / 16), (0, 1, 5 / 16), (1, 1, 1 / 16)],
    "atkinson": [(1, 0, 1 / 8), (2, 0, 1 / 8), (-1, 1, 1 / 8), (0, 1, 1 / 8), (1, 1, 1 / 8), (0, 2, 1 / 8)],
    "jarvis": [
        (1, 0, 7 / 48), (2, 0, 5 / 48),
        (-2, 1, 3 / 48), (-1, 1, 5 / 48), (0, 1, 7 / 48), (1, 1, 5 / 48), (2, 1, 3 / 48),
        (-2, 2, 1 / 48), (-1, 2, 3 / 48), (0, 2, 4 / 48), (1, 2, 3 / 48), (2, 2, 1 / 48),
    ],
    "stucki": [
        (1, 0, 8 / 42), (2, 0, 4 / 42),
        (-2, 1, 2 / 42), (-1, 1, 4 / 42), (0, 1, 8 / 42), (1, 1, 4 / 42), (2, 1, 2 / 42),
        (-2, 2, 1 / 42), (-1, 2, 2 / 42), (0, 2, 4 / 42), (1, 2, 2 / 42), (2, 2, 1 / 42),
    ],
    "burkes": [(1, 0, 8 / 32), (2, 0, 4 / 32), (-2, 1, 2 / 32), (-1, 1, 4 / 32), (0, 1, 8 / 32), (1, 1, 4 / 32), (2, 1, 2 / 32)],
    "sierra3": [
        (1, 0, 5 / 32), (2, 0, 3 / 32),
        (-2, 1, 2 / 32), (-1, 1, 4 / 32), (0, 1, 5 / 32), (1, 1, 4 / 32), (2, 1, 2 / 32),
        (-1, 2, 2 / 32), (0, 2, 3 / 32), (1, 2, 2 / 32),
    ],
    "sierra2": [(1, 0, 4 / 16), (2, 0, 3 / 16), (-2, 1, 1 / 16), (-1, 1, 2 / 16), (0, 1, 3 / 16), (1, 1, 2 / 16), (2, 1, 1 / 16)],
}

# OpenDisplay DitherMode 名 -> 矩阵名
OD_DITHER_MAP = {
    "FLOYD_STEINBERG": "floydSteinberg", "ATKINSON": "atkinson",
    "JARVIS_JUDICE_NINKE": "jarvis", "STUCKI": "stucki", "BURKES": "burkes",
    "SIERRA": "sierra3", "SIERRA_LITE": "sierra2",
}

# 加权 RGB 量化的亮度项（对齐 compare.html nIdx 的 palL，顺序同 OD_PALETTE）
_OD_PAL_L = (0.0, 1.0, 0.6, 0.25, 0.4, 0.35)


# ---------------- 调色（对齐前端滑块，乘法对齐 tone/DRC） ----------------

def apply_tone(arr: np.ndarray, br: int = 0, ct: int = 0, st: int = 0) -> np.ndarray:
    """亮度/对比度/饱和度。br/ct/st ∈ [-50, 50]。公式对齐 compare.html processA。"""
    a = arr.astype(np.float32)
    bf, cf, sf = 1 + br / 100, 1 + ct / 100, 1 + st / 100
    a = a * bf
    a = (a - 128) * cf + 128
    gray = a @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
    a = gray[..., None] + (a - gray[..., None]) * sf
    return np.clip(a, 0, 255)


def sharpen(arr: np.ndarray, amount: int = 0) -> np.ndarray:
    """十字邻域锐化。amount ∈ [0, 100]，t=amount/100，center=1+4t。对齐 compare.html sharpen。"""
    if amount <= 0:
        return arr.astype(np.float32)
    a = arr.astype(np.float32)
    t = amount / 100
    ctr, n = 1 + 4 * t, -t
    out = a * ctr
    out[1:, :] += a[:-1, :] * n      # 加左邻
    out[:-1, :] += a[1:, :] * n      # 加右邻
    out[:, 1:] += a[:, :-1, :] * n   # 加上邻
    out[:, :-1] += a[:, 1:, :] * n   # 加下邻
    return np.clip(out, 0, 255)


# ---------------- 颜色空间 / 量化 ----------------

def srgb_to_lab(rgb: np.ndarray) -> np.ndarray:
    """sRGB(0-255) -> Lab。rgb: (N,3) 或 (3,)。返回 (N,3) 或 (3,)。"""
    s = rgb.astype(np.float32) / 255.0
    one_d = s.ndim == 1
    if one_d:
        s = s[None, :]
    lin = np.where(s <= 0.04045, s / 12.92, ((s + 0.055) / 1.055) ** 2.4)
    m = np.array(
        [[0.4124564, 0.3575761, 0.1804375],
         [0.2126729, 0.7151522, 0.0721750],
         [0.0193339, 0.1191920, 0.9503041]],
        dtype=np.float32,
    )
    xyz = lin @ m.T
    x, y, z = xyz[:, 0] / 0.95047, xyz[:, 1], xyz[:, 2] / 1.08883
    xyz2 = np.stack([x, y, z], axis=1)
    gt = xyz2 > 0.008856
    f = np.where(gt, xyz2 ** (1 / 3), 7.787 * xyz2 + 16 / 116)
    fx, fy, fz = f[:, 0], f[:, 1], f[:, 2]
    lab = np.stack([116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)], axis=1)
    return lab[0] if one_d else lab


def _quantize_lab(p: np.ndarray, pal: np.ndarray, pal_lab: np.ndarray) -> int:
    lab = srgb_to_lab(p)
    d = np.sum((pal_lab - lab) ** 2, axis=1)
    return int(np.argmin(d))


def _quantize_weighted_rgb(p: np.ndarray, pal: np.ndarray) -> int:
    """对齐 compare.html nIdx：加权 RGB 距离 + 亮度项。"""
    r, g, b = p[0], p[1], p[2]
    l1 = (r * 250 + g * 350 + b * 400) / 255000.0
    best, mind = 0, 1e30
    for i, c in enumerate(pal):
        dr, dg, db = r - c[0], g - c[1], b - c[2]
        rd = (dr * dr * 0.25 + dg * dg * 0.35 + db * db * 0.40) * 0.75 / 65025.0
        ld = (l1 - _OD_PAL_L[i]) ** 2
        d = 1.5 * rd + 0.60 * ld
        if d < mind:
            mind, best = d, i
    return best


# ---------------- 误差扩散 ----------------

def error_diffuse(
    arr: np.ndarray,
    palette: list[tuple[int, int, int]],
    matrix: list[tuple[int, int, float]],
    serpentine: bool = True,
    diff_strength: float = 1.0,
    color_matching: str = "lab",
) -> np.ndarray:
    """误差扩散抖动。返回索引数组 (H, W) uint8。"""
    h, w = arr.shape[:2]
    pal = np.array(palette, dtype=np.float32)
    pal_lab = srgb_to_lab(pal) if color_matching == "lab" else None
    indices = np.zeros((h, w), dtype=np.uint8)
    cur = arr.astype(np.float32).copy()
    for y in range(h):
        rev = serpentine and (y % 2 == 1)
        xs = range(w - 1, -1, -1) if rev else range(w)
        for x in xs:
            p = cur[y, x]
            if color_matching == "lab":
                idx = _quantize_lab(p, pal, pal_lab)
            else:
                idx = _quantize_weighted_rgb(p, pal)
            indices[y, x] = idx
            err = (cur[y, x] - pal[idx]) * diff_strength
            for dx, dy, f in matrix:
                nx = x - dx if rev else x + dx
                ny = y + dy
                if 0 <= nx < w and 0 <= ny < h:
                    cur[ny, nx] = np.clip(cur[ny, nx] + err * f, 0, 255)
    return indices


# ---------------- 输出 ----------------

def indices_to_image(indices: np.ndarray, colors: list[tuple[int, int, int]]):
    """索引数组 -> PIL RGB 图。"""
    from PIL import Image

    h, w = indices.shape
    img = np.zeros((h, w, 3), dtype=np.uint8)
    for i, c in enumerate(colors):
        img[indices == i] = c
    return Image.fromarray(img)


def to_6color_bmp(img, width: int, height: int) -> bytes:
    """24-bit BGR bottom-up BMP。完全照抄 compare.html buildBmpRaw。"""
    if not isinstance(img, np.ndarray):
        img = np.array(img)
    row_size = ((3 * width + 3) // 4) * 4
    pad = row_size * height
    total = 54 + pad
    bmp = bytearray(total)
    struct.pack_into("<2sIHHI", bmp, 0, b"BM", total, 0, 0, 54)
    struct.pack_into("<IiiHHIIiiII", bmp, 14, 40, width, height, 1, 24, 0, pad, 2835, 2835, 0, 0)
    for y in range(height):
        row = img[height - 1 - y]                    # bottom-up：首行=原图最底行
        row_bytes = row[:, ::-1].astype(np.uint8).tobytes()  # RGB -> BGR
        start = 54 + y * row_size
        bmp[start:start + 3 * width] = row_bytes
    return bytes(bmp)


# ---------------- 两套管线统一入口 ----------------

def render_image_6color(
    img,
    pipe: str = "epd",
    br: int = 0,
    ct: int = 20,
    st: int = 20,
    sh: int = 50,
    dither: str = "floydSteinberg",
    df: int = 100,
) -> dict:
    """渲染成 6 色。返回 {"preview": PIL(校准色), "bmp": bytes(设备纯色 BMP), "size": (w,h)}。

    pipe="epd"：tinted 校准色板 + Lab 量化 + replaceColors 转设备纯色
    pipe="od" ：前置 BCS + 纯色板加权 RGB 量化
    """
    if pipe not in ("epd", "od"):
        raise ValueError(f"pipe 必须是 'epd' 或 'od'，收到 {pipe!r}")
    if dither in OD_DITHER_MAP:
        dither = OD_DITHER_MAP[dither]
    if dither not in ERROR_DIFFUSION_MATRICES:
        raise ValueError(f"不支持的抖动算法 {dither!r}")

    from PIL import Image

    if not isinstance(img, Image.Image):
        img = Image.open(img)
    img = img.convert("RGB")
    arr = np.array(img).astype(np.float32)
    h, w = arr.shape[:2]

    arr = apply_tone(arr, br, ct, st)
    arr = sharpen(arr, sh)
    matrix = ERROR_DIFFUSION_MATRICES[dither]
    strength = df / 100

    if pipe == "od":
        indices = error_diffuse(arr, OD_PALETTE, matrix, diff_strength=strength, color_matching="rgb")
        preview = indices_to_image(indices, [DEV_TO_CAL[c] for c in OD_PALETTE])
        dev_img = indices_to_image(indices, OD_PALETTE)
    else:
        indices = error_diffuse(arr, EPD_CAL_PALETTE, matrix, diff_strength=strength, color_matching="lab")
        preview = indices_to_image(indices, EPD_CAL_PALETTE)
        dev_img = indices_to_image(indices, EPD_DEV_PALETTE)

    return {"preview": preview, "bmp": to_6color_bmp(dev_img, w, h), "size": (w, h)}


# ---------------- 推送（Phase 2 用） ----------------

def push_to_esp32(bmp_bytes: bytes, host: str, port: int = 80, name: str = "ai_.bmp", timeout: int = 60) -> dict:
    """POST BMP 到相框 /api/upload。"""
    import requests

    try:
        r = requests.post(
            f"http://{host}:{port}/api/upload", params={"name": name}, data=bmp_bytes, timeout=timeout
        )
        return {"ok": r.status_code == 200, "http": r.status_code, "body": r.text[:200]}
    except Exception as e:  # noqa: BLE001
        return {"ok": False, "error": str(e)}


# ---------------- 自测 ----------------

def _self_test() -> None:
    from PIL import Image

    np.random.seed(0)
    # 480x800 渐变 + 色块，覆盖所有颜色
    x = np.linspace(0, 255, 480, dtype=np.float32)
    y = np.linspace(0, 255, 800, dtype=np.float32)
    xx, yy = np.meshgrid(x, y)
    grad = np.stack([xx, yy, 255 - xx], axis=-1).astype(np.uint8)
    grad[:80, :, :] = (0, 0, 0)       # 黑
    grad[80:160, :, :] = (255, 255, 255)  # 白
    grad[160:240, :, :] = (255, 255, 0)   # 黄
    grad[240:320, :, :] = (255, 0, 0)     # 红
    grad[320:400, :, :] = (0, 0, 255)     # 蓝
    grad[400:480, :, :] = (0, 255, 0)     # 绿
    grad[480:560, :, :] = (0, 0, 0)
    grad[560:640, :, :] = (255, 255, 255)
    grad[640:720, :, :] = (255, 255, 0)
    grad[720:800, :, :] = (255, 0, 0)
    img = Image.fromarray(grad)

    for pipe in ("epd", "od"):
        res = render_image_6color(img, pipe=pipe, dither="floydSteinberg", br=0, ct=0, st=0, sh=0)
        w, h = res["size"]
        bmp = res["bmp"]
        file_size = struct.unpack_from("<I", bmp, 2)[0]
        row_size = ((3 * w + 3) // 4) * 4
        assert file_size == 54 + row_size * h, f"{pipe} 文件大小错误 {file_size}"
        assert struct.unpack_from("<I", bmp, 14)[0] == 40
        assert struct.unpack_from("<i", bmp, 18)[0] == w
        assert struct.unpack_from("<i", bmp, 22)[0] == h
        assert struct.unpack_from("<H", bmp, 28)[0] == 24
        # 仅 6 纯色
        dev = set()
        for y in range(0, h, 4):
            for x in range(0, w, 4):
                i = 54 + (h - 1 - y) * row_size + x * 3
                dev.add((bmp[i], bmp[i + 1], bmp[i + 2]))
        print(f"[{pipe}] BMP {w}x{h} {file_size}B, 抽样纯色数={len(dev)}")
        res["preview"].save(f"_selftest_{pipe}.png")
    print("self-test OK")


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        _self_test()
    else:
        print(__doc__)
