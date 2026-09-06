# AI-Photo-Picker

> AI 智能选片 → 墨水屏 6 色渲染 → 一键推送 ESP32 相框

本项目是 [`ESP32-S3-PhotoPainter`](../..)（Waveshare 7.3 寸墨水屏电子相框）的 **PC 端配套工具**。它用一个**视觉语言模型（VLM）**为整本相册打分、写文案，然后按“历史上的今天”挑选照片，把它渲染成相框固件能直接接收的**6 色 BMP**，并通过 OTA 接口推到相框上展示。

一句话工作流：

```
相册目录 ──analyze──▶ photos.db（评分/文案/EXIF/城市）
                         │
                         │  render（历史上的今天选片）
                         ▼
        照片 + 文案/日期/地点 ──▶ 6 色渲染（epd / od 管线）──▶ ESP32 相框 /api/upload
```

---

## 功能概览

| 子命令 | 作用 |
|--------|------|
| `analyze` | 扫描相册，调用 VLM 为每张照片打多维度评分、写旁白文案、提取 EXIF/GPS，并反查中文城市名，结果写入 `photos.db` |
| `render`  | 按“历史上的今天”选片（可回溯 365 天），合成“照片 + 文案 + 日期 + 地点”，渲染成 6 色 BMP，落盘并（可选）推送到相框 |
| `serve`   | 启动 Flask WebUI：照片库浏览 / 渲染模拟器 / 调色 / 推送 / 设置管理 / 一键触发 analyze、render |

---

## 项目结构

```
AI-Photo-Picker/
├── main.py               # 统一入口：analyze / render / serve 三个子命令
├── analyze_photos.py     # VLM 打分 + 文案 + EXIF/GPS + 城市反查（含 NAS 掉盘守护、渠道故障切换）
├── render_daily_photo.py # “历史上的今天”选片 + 合成构图（照片/文案/日期/地点）
├── render_pipeline.py    # 6 色渲染引擎：epd / od 两套管线、误差扩散抖动、BMP 编码、推送
├── server.py             # Flask WebUI（review / sim / settings / api/*）
├── config.py             # 运行时配置：从 settings.json 加载（WebUI 维护）
├── config-example.py     # 配置参考模板（含注释），不改代码即默认值
├── settings_store.py     # settings.json 读写 + 默认值合并
├── settings.json         # 实际配置（含 API Key、相册路劲等，已在 .gitignore 中）
├── requirements.txt      # Python 依赖
├── data/world_cities_zh.csv  # geonames 中文城市名索引（二万三千余条）
├── webui/                # 前端页面
│   ├── picker.html       # 交互式渲染 + 调色 + 推送页
│   └── lib/              # epdoptimize.js / opendisplay.js（前端渲染 bundle，与固件同源）
├── scripts/daily_render.sh   # 每日定时渲染脚本（cron / 计划任务可用）
├── output/               # 渲染产物（*_bmp / preview_*.png，在 .gitignore 中）
├── logs/                 # analyze.log / render.log（在 .gitignore 中）
├── photos.db             # SQLite 评分数据库（在 .gitignore 中）
└── filelist.txt          # 最近一次扫描的文件列表（在 .gitignore 中）
```

---

## 安装

```bash
cd AI-Photo-Picker
python -m venv venv

# Windows
venv\Scripts\activate
# macOS / Linux
source venv/bin/activate

pip install -r requirements.txt
```

依赖：`Flask`、`requests`、`Pillow`、`pillow-heif`（HEIC）、`numpy`。

> `exiftool` 是**可选**的：装了能拿到更完整的 GPS/EXIF；没装流程也能跑，只是部分 GPS 字段缺省。见 `analyze_photos.py` 里的安装提示。
> 需要 HEIC 支持时确保 `pillow-heif` 已安装（代码里已 `register_heif_opener()`）。

---

## 配置

配置优先从 **`settings.json`** 读取（由 WebUI 的「设置」页维护，`.gitignore` 已忽略），`config.py` 在启动时加载它；脚本里统一用 `getattr(cfg, "KEY", default)` 读取，键缺失时落到脚本内置默认值。

常用的关键配置（详见 `config-example.py` 的逐项注释）：

| 键 | 含义 |
|----|------|
| `IMAGE_DIR` | 相册目录（`analyze` 扫描源，WebUI 图片浏览时 `IMAGE_DIR` 决定 `_make_image_url` 的可见范围） |
| `DB_PATH` | SQLite 数据库路径 |
| `API_CHANNELS` | VLM 渠道列表（按优先级排序，遇 429 / 失败自动切换下一个）；每条含 `api_url / api_key / model_name` |
| `BATCH_LIMIT` | 每次分析的照片数量上限（`None` 为不限） |
| `TIMEOUT` | 单次 VLM 请求超时（秒） |
| `VLM_MAX_LONG_EDGE` | 送给 VLM 前图片长边缩放上限（云端建议调低省 token/成本） |
| `WORLD_CITIES_CSV` | 中文城市索引（geonames） |
| `CITY_GRID_DEG` | 城市反查网格大小（度），越大越快精度略低 |
| `HOME_LAT / HOME_LON / HOME_RADIUS_KM` | “常驻地”坐标，用于判断异地给回忆分小幅加分 |
| `CITY_MAX_DISTANCE_KM` | 判定“就在某城市附近”的最大距离 |
| `MEMORY_THRESHOLD` | 选片“回忆度”阈值 |
| `DAILY_PHOTO_QUANTITY` | 每日选片数量 |
| `ESP32_HOST / ESP32_PORT / PUSH_ENABLED` | 相框地址与是否推送 |
| `AI_NAME_PREFIX / AI_MAX_FILES` | 推送到相框的文件名前缀 / 相框自动清理阈值（与固件常量对齐） |
| `RENDER_PIPELINE / RENDER_DITHER / RENDER_ADJ` | 渲染方案、抖动算法、默认调色（亮度/对比度/饱和度/锐化/扩散） |
| `OUTPUT_DIR / FONT_PATH` | 渲染输出目录 / 自定义字体（空则用默认字体） |
| `FLASK_HOST / FLASK_PORT / ENABLE_REVIEW_WEBUI` | WebUI 监听与是否开放照片库浏览 |

> ⚠️ `settings.json` 中的 **API Key 属于敏感信息**，已在 `.gitignore` 中排除，请勿提交到版本库。

---

## 使用方法

### CLI

```bash
# 1) 分析相册（VLM 打分 + 文案 + EXIF），可开多线程，--debug 打印请求/响应
python main.py analyze [-j 4] [--cache] [--debug]

# 2) 渲染“历史上的今天”并推送相框
python main.py render [--today 2024-03-18] [--count 5]
                     [--pipeline epd|od] [--dither floydSteinberg]
                     [--no-push] [--output ./output]

# 3) 启动 WebUI（默认 0.0.0.0:8765）
python main.py serve [--host 0.0.0.0] [--port 8765]
```

`render` 默认读 `settings.json` 中的 pipeline / dither / 调色与推送开关；`--no-push` 只落盘不推送到相框。

### WebUI（`serve`）

启动后本机访问 `http://127.0.0.1:8765/`（主页面为 `/review`），常见页面：

| 路由 | 说明 |
|------|------|
| `/review` | 照片库浏览：分页、按月份/日期/类型筛选、7 种排序、随机一天 |
| `/sim`   | 墨水屏渲染 + 调色 + 推送模拟器（两套管线并排对比） |
| `/picker`| 交互式渲染 + 调色 + 推送页（独立于 `sim` 的简化版） |
| `/settings` | 编辑配置（相册路径、VLM 渠道、推送开关、调色默认值等） |
| `/api/analyze`, `/api/render` | 一键触发分析 / 渲染（后台子进程） |
| `/api/stop` | 停止正在运行的 analyze / render 子进程 |
| `/api/log/<name>` | 读取 analyze / render 子进程的实时日志 |
| `/api/settings` | 配置的读取 / 保存 |
| `/api/md_list` | 全库“真实存在的 MM-DD 日期集合”，供“随机一天”使用 |

> 照片库浏览依赖 `ENABLE_REVIEW_WEBUI = true`；跑通后如果只在 ESP32 侧下载成品，可关闭它（相关页面会 404，ESP32 下载接口不受影响）。

---

## 渲染管线（6 色）

`render_pipeline.py` 移植自相框仪表盘的两套前端渲染方案，输出 **24-bit BGR bottom-up BMP**，可直接被相框 `/api/upload` 接收。

- **`epd`（epdoptimize 风格）**：tinted 校准色板 + **Lab 最近邻量化** + 误差扩散 + 转设备纯色（`replaceColors`）。
- **`od`（OpenDisplay 风格）**：前置 BCS 调色（亮度/对比度/饱和度）+ 纯色板**加权 RGB 量化** + 误差扩散。

两套管线都输出固定的 **6 色**：黑 / 白 / 黄 / 红 / 蓝 / 绿（顺序因管线而异，见 `render_pipeline.py` 的调色板常量）。误差扩散支持 7 种矩阵：`floydSteinberg / atkinson / jarvis / stucki / burkes / sierra3 / sierra2`，可开启蛇形（serpentine）抖动。

`render_image_6color()` 返回 `{"preview": PIL(校准色), "bmp": bytes(设备纯色 BMP), "size": (w,h)}`；`push_to_esp32()` 负责 POST 到相框。

---

## 每天自动出图

`scripts/daily_render.sh` 用 **mkdir 锁**防止并发重复渲染，直接调用 `main.py render`，日志写入 `logs/render.log`。在 macOS 用它配 `cron / launchd`，或在系统「计划任务」里定期执行即可：

```bash
# 示例：每天 08:00 渲染
0 8 * * * /path/to/AI-Photo-Picker/scripts/daily_render.sh
```

脚本假定一个 `venv` 位于项目目录下（`$PROJECT_DIR/venv`），并按需调整 `PROJECT_DIR`。

---

## 数据模型（photos.db）

核心表 `photo_scores`，键为照片绝对路径 `path`（主键）。主要列：

- `caption`（画面描述）、`type`（类型，可 `/` 分隔多值）、`reason`（打分理由）
- `memory_score` / `beauty_score`（真实照片）；`funny_score` / `depth_score` / `art_score`（表情包/梗图/二次元插画的类型专属评分）
- `side_caption`（一句话旁白文案，渲染时显示在照片下方）
- `width` / `height` / `orientation`（横竖屏）
- `used_at`（已上屏时间，用于 review 界面的“已上屏”展示）
- `exif_json`（EXIF 全量 JSON，供 `datetime` / `make` / `model` / 曝光参数 / GPS 等解析）
- `exif_gps_lat` / `exif_gps_lon` / `exif_city`（解出的经纬度与中文城市名）
- `exif_datetime` / `exif_make` / `exif_model` / `exif_iso` / `exif_exposure_time` / `exif_f_number` / `exif_focal_length`
- `raw_json`（VLM 原始返回，便于排查）

建议先跑一次 `analyze` 生成数据再 `serve`；服务端 `_ensure_schema()` 会在旧库缺列时自动补列。

---

## 与 ESP32 相框的对接

- 渲染产物文件名形如 `ai_<时间戳>_<idx>.bmp`；`AI_NAME_PREFIX` 前缀用于相框固件识别来源文件（配合其内部的 `AI_MAX_FILES` 清理机制，默认 300 张自动清理）。
- 推送使用相框固件的 `POST /api/upload?name=<name>` 接口（见 `render_pipeline.push_to_esp32` 与 WebUI 前端相同的上传逻辑）。
- 相框 IP：AP 模式通常为网关 `192.168.4.1`；STA 模式填分配到的 DHCP 地址。
- 相框端的模块（`lib/epdoptimize.js` / `lib/opendisplay.js`）与固件仪表盘同源，保证“所见即所得”。

---

## 备注与已知细节

- **标题里的「历史上的今天」**：按月日匹配（如 12-02），在所有年份该月日的照片里挑 `memory > MEMORY_THRESHOLD` 的候选；当天没有就**往前回溯 365 天**；365 天内都没有则退回全局回忆度最高的作为兜底。
- **异地加分**：照片 GPS 距离 `HOME_LAT/LON` 超过 `HOME_RADIUS_KM` 视为“异地”，回忆分小幅 +5。
- **Screenshot 过滤**：文件名含 `screenshot` 的图会被 `analyze` / `render` 双双跳过。
- **网络图不入“回忆/美观”分**：表情包 / 梗图 / 二次元插画不评 `memory_score` / `beauty_score`（输出 `null`），改为评 `funny_score` / `depth_score` / `art_score`。
- **VLM 渠道故障切换**：渠道列表有多个时按优先级请求，遇 429 / 解析失败自动切下一个，并对失败渠道做**冷却降级**（`CHANNEL_FAILOVER_COOLDOWN_SEC`）。
- **NAS 掉盘守护**（macOS）：路径若在 NAS 卷上，读文件失败会尝试用 AppleScript 重挂载并重试（配置 `NAS_MOUNT_URL` / `NAS_MOUNT_POINT`）。
- **渲染方向**：`render_pipeline` 按“高 > 宽用竖屏 480×800，否则横屏 800×480”自动适配；`render_daily_photo` 固定按竖屏 480×800 构图（照片 + 底部文案区）。

更多关于墨水屏固件（解析、刷新、SD 卡目录、NVS 模式等）的说明见上层 `ESP32-S3-PhotoPainter/CLAUDE.md`。
