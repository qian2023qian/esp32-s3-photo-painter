# ESP32-S3 6-Color PhotoFrame

> 基于 **微雪官方固件**（[ESP32-S3-PhotoPainter](https://www.waveshare.net/wiki/ESP32-S3-PhotoPainter)）修改的 **7.3 寸六色墨水屏智能相框**。
> 微雪那份官方固件本身是 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的衍生（其例程目录即 `01_Example/xiaozhi-esp32`），本固件的「小智 AI 语音」部分由此而来。

一块 ESP32-S3 驱动的电子相框：静态展示 **Waveshare 7.3" Spectra 6 墨水屏 (800×480)**，支持 SD 卡本地轮播、WiFi 配网、Web 仪表盘上传/调色/推送，并集成 **小智 AI 语音交互**。

- **屏幕**：GDEP073E01，6 色（黑 / 白 / 黄 / 红 / 蓝 / 绿）
- **主控**：ESP32-S3，16 MB Flash，8 MB Octal PSRAM，240 MHz
- **音频**：ES8311 (DAC) + ES7210 (ADC)，I2S，24 kHz
- **电源**：AXP2101 PMU；**温湿度**：SHTC3 (I2C)；**存储**：Micro SD (SDMMC)
- **配套**：PC 端 `AI-Photo-Picker`（VLM 选片 → 6 色渲染 → 推送）

---

## 目录

- [硬件与引脚](#硬件与引脚)
- [编译与烧录](#编译与烧录)
- [工作模式](#工作模式)
- [基本操作](#基本操作)
- [Web 仪表盘](#web-仪表盘)
- [分类目录与轮播方式](#分类目录与轮播方式)
- [批量上传](#批量上传)
- [六色渲染管线](#六色渲染管线)
- [上传文件名格式](#上传文件名格式)
- [HTTP API](#http-api)
- [小智语音 · MCP 工具](#小智语音--mcp-工具)
- [SD 卡目录](#sd-卡目录)
- [项目结构](#项目结构)
- [注意事项 · 已知陷阱](#注意事项--已知陷阱)
- [相关项目](#相关项目)

---

## 硬件与引脚

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-S3, 16MB Flash, Octal PSRAM | — |
| 屏幕 | Waveshare 7.3" Spectra 6（GDEP073E01, 800×480） | SPI/epaper |
| 音频 DAC | ES8311 | I2S |
| 音频 ADC | ES7210 | I2S |
| 电源 | AXP2101 PMU | I2C |
| 温湿度 | SHTC3 | I2C |
| 存储 | Micro SD 卡 | SDMMC |
| 按键 | BOOT / KEY / PWR | GPIO |

引脚定义见 `main/boards/waveshare-s3-PhotoPainter/config.h`：

| 功能 | 引脚 |
|------|------|
| I2S MCLK / WS / BCLK | 14 / 16 / 15 |
| I2S DIN / DOUT | 18 / 17 |
| Codec I2C SDA / SCL | 47 / 48 |
| Codec PA 使能 | GPIO 7 |
| BOOT 按键 | GPIO 0 |
| KEY 按键 | GPIO 4 |
| PWR 按键 | GPIO 5 |

---

## 编译与烧录

- **IDF 版本**：ESP-IDF v5.5.x（本机工具链 `v5.5.4`）
- **目标**：`esp32s3`；**开发板**：`waveshare-s3-PhotoPainter`

Windows PowerShell 一键构建（清理式）：

```powershell
.\build_demo.ps1
```

或手动构建（先加载 IDF 环境）：

```powershell
. 'C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1'
idf.py build
idf.py -p COMx flash monitor
```

> 根 `CMakeLists.txt` 已配置 `CMAKE_NINJA_FORCE_RESPONSE_FILE`，规避 Windows 命令行超长问题。
> `sdkconfig.defaults` 设定 IDF 5.5.x + esp32s3；`sdkconfig.defaults.esp32s3` 开启 16MB Flash (QIO)、240 MHz、Octal PSRAM 80 MHz。

---

## 工作模式

启动时从 NVS 命名空间 **`PhotoPainter`**、键 **`PhotPainterMode`**（⚠️ 注意拼写少一个 `o`，历史遗留，勿改）读取运行模式：

| 值 | 模式 | 说明 |
|----|------|------|
| 0x01 | **PhotoFrame**（默认） | SD 卡轮播 + WiFi 配网 + Web 仪表盘 |
| 0x03 | XiaoZhi | AI 语音交互（小智） |
| 0x04 | Mode Selection | 启动时按键选择模式 |

默认首次启动为 **PhotoFrame (0x01)**。模式切换通过写 NVS + `esp_restart()` 实现。

---

## 基本操作

| 操作 | 功能 |
|------|------|
| BOOT（GPIO 0）短按 | 切换到下一张图片 |
| BOOT（GPIO 0）双击 | 显示中文电量页（充电状态 / 阶段 / 电压 / 电量） |
| KEY（GPIO 4）长按 5 秒 | 进入模式选择界面，选择后重启 |
| PWR（GPIO 5） | 预留 |

**上电**：默认进入 PhotoFrame 模式，自动轮播 `/sdcard/photos/` 的图片。WiFi 优先连已保存凭据（STA），失败 3 次后降级开启 AP 热点（`PhotoFrame / 12345678`）。

> 当前**无待机/休眠**：设备上电后持续运行。深度睡眠代码已注释（`#if 0`），需要待机时取消注释 `esp_deep_sleep_start()` 调用再编译。

---

## Web 仪表盘

手机/电脑连上相框 WiFi 后，浏览器访问设备 IP 或 AP 热点 `http://192.168.4.1`。

| 功能 | 说明 |
|------|------|
| 设备状态 | 页面自动刷新：WiFi、电量、传感器、图片数、轮播状态 |
| 电量显示 | 顶栏电量徽章（低电量 <10% 变红）+ 相框状态卡片电池电压/充电状态 |
| WiFi 配网 | 扫描 → 选 SSID → 输密码 → 连接；STA 失败自动降级 AP |
| 上传照片 | 选**单张** → 实时预览原图 vs 墨水屏效果 → 旋转 →「显示到屏幕」（上传并立刻上屏） |
| 批量上传 | 一次**多选** → 串行处理上传（只写卡、不刷屏）→ 本地缩略图结果条 → 结束后跳到新增页并高亮，**点高亮缩略图即可上屏**，见 [批量上传](#批量上传) |
| 删除照片 | 图片列表点 `x` |
| 图片列表 | 分页（`pageStore` 缓存已访问页，翻回秒开）；缩略图 400px 降采样顺序加载 |
| 图像调节 | 亮度 / 对比度 / 饱和度 / 锐化 / 抖动矩阵 / 扩散强度（实时） |
| 双管线 | epdoptimize / OpenDisplay 一键切换，并排对比 |
| 轮播设置 | 修改切换间隔（**「时」「分」两个数字输入框**，不用输入冒号）＋ 自动轮播开关（**开关点一下就立即存入 NVS**，断电/重启后仍保持） |
| 播放目录 | 下拉选择 `/sdcard/photos/` 下的分类目录（或根目录）；旁边可**新建分类目录**，见 [分类目录与轮播方式](#分类目录与轮播方式) |
| 轮播方式 | 顺序 / 倒序 / 随机，切换后立即生效并持久化 |
| 休眠时段 | 设置开始 / 结束时间 |
| 重启 | 页面底部红色按钮 |

> 图片处理全部在**客户端浏览器**完成（Canvas 缩放 + 6 色抖动 → BMP），ESP32 端只负责写 SD 卡 + 刷新墨水屏，不消耗设备算力。

---

## 分类目录与轮播方式

把图片分门别类放在 `/sdcard/photos/` 的**一级子目录**里，然后在仪表盘上选择要播放哪个目录、按什么顺序播放。

### 目录

- 仪表盘「相框设置 → 新建分类目录」填名字点「创建」，等价于 `mkdir /sdcard/photos/<名字>`；也可以直接在电脑上往卡里建目录。
- 「播放目录」下拉框列出**根目录**＋所有一级子目录，选中即切换：
  重新扫描该目录 → 索引归零 → 立刻上屏 → 写入 NVS。
- **上传的图片会写进当前选中的目录**，图片列表/缩略图/删除也都作用于当前目录——所以「切目录 → 上传」就是分类归档的完整流程。
- 目录名规则（前后端一致校验）：**单层**、非空、不含 `/` `\` `..`、不含 `"`（目录名会写进 NVS 的 JSON，带引号会让设置整份解析失败）、**≤ 32 字符**。
- 若保存的目录被外部删掉/改名，开机或重扫时会自动**回退到根目录**并打日志，不会一直卡在「无图片」。

### 轮播方式（`mode`）

| 值 | 方式 | 行为 |
|----|------|------|
| 0 | 顺序 | `index = (index + 1) % count` |
| 1 | 倒序 | `index = (index - 1 + count) % count` |
| 2 | 随机 | **洗牌不重复**：每轮把 `0..count-1` 用 Fisher-Yates 洗一次按序播放，一轮走完再重洗，并避免跨轮连续重复同一张 |

- 三种方式对**自动轮播**、**BOOT 短按切图**、**HA/MQTT 的 next/prev** 都生效（统一走 `photo_calc_next()`）。
- 随机序存在 PSRAM（`uint16_t × count`），目录或数量变化时自动重洗；分配失败会退化为顺序播放并打警告。

### 持久化

`dir` 与 `mode` 和 `interval` / `running` / `sleep_*` 一起写在 NVS 命名空间 `photoframe` 的 `interval` 键里（同一份 JSON），断电重启后恢复。

---

## 批量上传

仪表盘的上传框支持**一次多选**：

- **选 1 张** → 走原来的单张流程（预览原图 vs 墨水屏效果、旋转、调色，点「显示到屏幕」＝上传并立刻上屏）。
- **选 ≥2 张** → 走批量流程：

| 环节 | 行为 |
|------|------|
| 处理方式 | **串行流水线**：读一张 → 缩放/抖动 → 上传 → 释放，内存里只留一张 BMP（单张 1.15MB，避免手机端内存爆掉） |
| 调节参数 | 整批复用当前「图像调节」设置（管线/亮度/对比度/饱和度/锐化/抖动/扩散） |
| 上传 | `POST /api/upload?name=…&show=0` —— **只落盘，不重扫、不上屏** |
| 反馈 | 每项一张**本地即时缩略图**（用校准色渲染，贴近实际上屏效果）＋ `处理中/上传中/已上传/失败` 状态 ＋ 总进度条 |
| 收尾 | 整批结束后调一次 `GET /api/photos?reload=1` 统一重扫 → **跳到新增文件所在页**并**高亮**这些项 |
| 上屏 | **不单独放按钮**：新增项已经在图片列表里高亮，点一下缩略图即可上屏（`switchPhoto` → `/api/switch`，按索引切换，不重复上传） |

### 为什么要 `show=0`

原来的上传流程是「传一张 = 立刻上屏」，而每次上屏都是一次**墨水屏整屏刷新**（明显闪烁、耗时较长）。批量如果沿用，N 张图就是 **N 次全目录重扫 + N 次整屏刷新**——体验会被刷新拖垮，而且用户并不想看每一张。所以把「上传」和「上屏」解耦：批量只写卡，上屏由用户逐张显式触发。

### 文件名为什么要加序号

命名规则是 `管线_抖动_br_ct_st_sh_df_时间戳`，而**时间戳只有秒级精度**。批量时同一秒内的多张会生成**同名文件互相覆盖**，最后只剩一张。所以批量会在末尾追加**批内序号**：`…_<时间戳>_<序号>.bmp`（单张流程不加，保持原命名）。

### 其他注意

- 批量期间会 `stopThumbs()` 暂停缩略图加载——ESP32 单线程 HTTP，缩略图会和上传抢连接。
- 整批结束后会恢复缩略图，并清掉分页缓存（`pageStore`）以保证列表是新的。
- 失败项会在结果条上标红，可单独重试（重选该文件即可）。
- 前端已做语法校验（`node --check`），但**批量流程未做实机验证**：尤其是长批次下手机端耗时（单张抖动 1~3 秒量级）与 SD 写入速度。

---

## 六色渲染管线

墨水屏支持 6 色（黑 / 白 / 黄 / 红 / 蓝 / 绿）。仪表盘内置两套专业抖动管线，把任意图片转换为可直接上屏的 6 色 BMP：

| 管线 | 引擎 | 说明 |
|------|------|------|
| `epdoptimize` | JavaScript | 默认，校准色板 + Lab 色差匹配 |
| `OpenDisplay` | WASM/Rust | 纯色板，加权 RGB/HWD 色彩方案 |

两者都支持图像预处理（亮度 / 对比度 / 饱和度 / 锐化）和多种误差扩散矩阵：**Floyd-Steinberg、Atkinson、Jarvis-Judice-Ninke、Stucki、Burkes、Sierra-3、Sierra-2**（可蛇形抖动）。

前端 bundle 由 ESP32 直接以 `/lib/epdoptimize.js`、`/lib/opendisplay.js` 提供（`components/user_app_bsp/mode_src/dashboard.html` 同源生成）。

> 两套管线的上游原项目：[paperlesspaper/epdoptimize](https://github.com/paperlesspaper/epdoptimize)（v1.3.0）与 [OpenDisplay/epaper-dithering](https://github.com/OpenDisplay/epaper-dithering)（v5.0.9，npm `@opendisplay/epaper-dithering`），以浏览器端 bundle 形式内嵌，各自遵循其上游许可。

---

## 上传文件名格式

上传后自动按参数缩写命名，ESP32 端按文件名反查索引，格式：

```
管线缩写_抖动缩写_亮度_对比度_饱和度_锐化_扩散_时间戳.bmp
```

例：`epd_fs_0_20_20_50_100_1764154920.bmp`

- 管线缩写：`epd`（epdoptimize）/ `od`（OpenDisplay）
- 抖动缩写：`fs` / `at` / `jjn` / `sk` / `bk` / `s3` / `s2`
- **批量上传会额外追加批内序号**：`…_<时间戳>_<序号>.bmp`。因为时间戳只有秒级精度，同秒内多张会同名互相覆盖；单张流程不加序号，保持原命名。

> ⚠️ 文件列表缓冲区 `sdcard_name[128]`，文件名（含路径）不能超过 127 字符。

---

## HTTP API

由 `components/user_app_bsp/mode_src/photo_web_server.cpp` 提供（默认端口 80），已开启 CORS：

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/` | 仪表盘 HTML |
| GET | `/api/status` | 设备状态（WiFi、电量、传感器、图片、设置、`dir`/`mode`） |
| GET | `/api/photos` | **当前播放目录**的图片列表（只有文件名 basename）；加 `?reload=1` 先重扫再返回（批量上传收尾 / 电脑直接拷图后刷新） |
| GET | `/api/dirs` | 列出 `/sdcard/photos` 下的一级分类目录 `{"dirs":["",...],"current":""}` |
| POST | `/api/mkdir` | 新建分类目录 `{"name":"cats"}` |
| GET | `/api/photo` | 单张图片数据（`?name=<basename>[&thumb=1]`，按当前播放目录解析） |
| GET | `/api/wifi/scan` | WiFi 扫描 |
| POST | `/api/wifi/connect` | 连接 WiFi `{"ssid","password"}` |
| POST | `/api/wifi/reset` | 重置 WiFi |
| POST | `/api/upload` | 上传图片（raw BMP，`?name=` 指定；**写入当前播放目录**）。加 `&show=0` 则只落盘、不重扫不上屏（批量上传用） |
| POST | `/api/delete` | 删除图片 `{"name"}`（按当前播放目录解析） |
| GET/POST | `/api/adjustments` | 读取 / 保存图像调节参数 |
| POST | `/api/settings` | 保存设置 `{"interval","running","sleep_start","sleep_end","dir","mode"}`（`interval` 单位是**分钟** 1~1440；仪表盘上用「时 / 分」输入框，按 时:分 显示） |
| POST | `/api/switch` | 切换到指定序号图片（15 秒冷却） |
| POST | `/api/reboot` | 重启 |
| GET | `/lib/epdoptimize.js` / `/lib/opendisplay.js` | 前端渲染 bundle |

---

## 小智语音 · MCP 工具

小智模式下（`esp32-s3-PhotoPainter.cc`），注册了供语音助手调用的 MCP 工具：

| 工具 | 功能 |
|------|------|
| `self.disp.SwitchPictures` | 切换到指定序号图片 |
| `self.disp.getNumberimages` | 获取当前图片总数 |
| `self.disp.imgloop` / `imgloopEit` | 进入 / 退出轮播模式 |
| `self.disp.imgsetTimerloop min` / `h` | 设置轮播间隔（分钟 / 小时） |
| `self.disp.isSHTC3` | 获取温湿度 |

小智图片目录为 `/sdcard/05_user_ai_img`；系统初始图与天气图标在 `/sdcard/01_sys_init_img`（0x01~0x08 对应晴/多云/雷雨/晴/小雨/下雪/中雨/阴等，见 `weather_app.cpp`）。

---

## SD 卡目录

| 目录 | 用途 |
|------|------|
| `/sdcard/photos/` | PhotoFrame 轮播图片（上传时自动转 6 色 BMP） |
| `/sdcard/photos/<分类>/` | PhotoFrame **分类目录**（一级子目录），可由仪表盘新建并在其中上传/播放 |
| `/sdcard/01_sys_init_img/` | 系统初始图 + 天气图标（小智模式） |
| `/sdcard/02_sys_ap_img/` | AP 配网模式显示图 |
| `/sdcard/03_sys_ap_html/` | AP 配网 Web 页面 |
| `/sdcard/05_user_ai_img/` | 小智/AI 上传图片 |
| `/sdcard/06_user_foundation_img/` | 用户底图 |

---

## 项目结构

```
ESP32-S3-6Color-PhotoFrame/
├── CMakeLists.txt               # 根构建（含 RESPONSE_FILE 配置）
├── build_demo.ps1               # Windows 清理式构建脚本
├── sdkconfig.defaults           # IDF 5.5.x, esp32s3
├── sdkconfig.defaults.esp32s3   # 16MB Flash(QIO), Octal PSRAM, 240MHz
├── main/
│   ├── main.cc                  # 入口：模式选择 + 事件循环
│   ├── application.cc/h         # 小智 AI 框架
│   ├── display/                 # 显示抽象
│   ├── audio/                   # 音频编解码服务
│   ├── boards/waveshare-s3-PhotoPainter/
│   │   ├── esp32-s3-PhotoPainter.cc  # 开发板定义 + MCP 工具
│   │   ├── config.h                   # 引脚定义
│   │   └── config.json                # 目标/构建元数据
│   └── assets/                  # 语言包、语音提示
├── components/
│   ├── app_bsp/                 # 应用层：weather、client、server(AP页)、imgdecode
│   ├── user_app_bsp/mode_src/   # 用户模式：PhotoFrame / XiaoZhi / Mode_Selection
│   │   ├── PhotoFrame_mode.cpp      # 相框模式核心逻辑
│   │   ├── xiaozhi_mode.cpp         # 小智 AI 模式
│   │   ├── Mode_Selection.cpp       # 启动模式选择
│   │   ├── photo_web_server.cpp     # 仪表盘 HTTP API
│   │   ├── dashboard.html           # ★ 仪表盘 UI（编辑这个）
│   │   ├── html_to_header.py        # 构建时 HTML→C++ 转换
│   │   ├── photo_web_pages.h        # 自动生成，勿手改
│   │   ├── epdoptimize_bundle.h     # 前端渲染 bundle
│   │   ├── opendisplay_bundle.h
│   │   ├── nvs_manager.c/h          # NVS 读写
│   │   ├── wifi_manager.c/h         # WiFi STA/AP 管理
│   │   └── mqtt_ha.cpp/h            # Home Assistant MQTT
│   ├── port_bsp/                # 硬件抽象：display、sdcard、button、led、i2c、fonts
│   ├── pmicpower/               # AXP2101 电源管理
│   └── codec_board/             # ES8311/ES7210 音频编解码
├── partitions/v2/               # 分区表（16m.csv 等）
├── managed_components/          # ESP-IDF 托管组件
└── tools/                       # gen_battery_font.py 等本地工具
```

**仪表盘开发流程**（HTML 已拆分为独立文件）：

1. 编辑 `components/user_app_bsp/mode_src/dashboard.html`
2. 运行 `build_demo.ps1`（自动用 `html_to_header.py` 重新生成 `.h`）
3. 烧录固件

> ⚠️ 不要直接编辑 `photo_web_pages.h`，它会被构建步骤覆盖。

---

## 注意事项 · 已知陷阱

**命名与协议**

- NVS 键拼写为 `PhotPainterMode`（**少一个 `o`**），历史遗留，不要改成 `PhotoPainterMode`。
- 轮播间隔在**界面上是「时」「分」两个数字输入框**（无需输入冒号，手机端点开即数字键盘），旁边实时显示换算结果；展示时（相框状态卡片、保存提示）用 `时:分` 形式。
- 但 HTTP 接口、NVS 与固件内部一律是**分钟**（`interval`，钳制 1~1440）——改格式只动表示层，不涉及数据迁移。保存时会把输入归一化（`0 时 90 分` → `1 时 30 分`，超过 24 时钳到 24 时）；若「时」「分」同时为 0 则拒绝保存，避免误设成 1 分钟（墨水屏每 1 分钟整屏刷新对面板很不友好）。
- 小智模式的 `imgsetTimerloop min` / `h` 是**另一套**间隔（小智模式自己的轮播定时），才区分分/时。
- 图片切换有 **15 秒冷却**（`photo_switch_to` 用 `esp_timer_get_time()` 微秒内部时钟，不依赖 NTP 对时）。
- 墨水屏刷新受 `epaper_gui_semapHandle` 互斥/信号量保护；图片切换经 `epaper_groups` EventGroup 触发。

**设置持久化（断电保存）**

- 相框设置（`interval` / `running` / `sleep_start` / `sleep_end` / `dir` / `mode`）以**完整 JSON** 存在 NVS 命名空间 `photoframe` 的 `interval` 键里，开机时整体恢复。
- **自动轮播开关点一下即保存**：前端立刻 `POST /api/settings {running:...}`，不需要再点「保存设置」，断电或重启后状态保持。
- 服务端落盘调用 `photo_persist_settings()` 写**完整规范状态**而不是请求原文——否则只改 `running` 的局部更新会把 `interval` / `sleep_*` 一起覆盖掉。
- `photo_running` **只表示用户开关**（持久化）；低电量暂停是临时状态 `low_battery_active`，不写 NVS，也**不会**被写进 NVS 覆盖用户开关。
- `interval` 在服务端钳制到 1~1440 分钟；仪表盘用「时 / 分」两个输入框采集（合计范围 1 分钟~24 小时），两者一致。

**低电量提醒 / 充电**

- **未充电**且电量 <10%（`LOW_BATTERY_ENTER`）时：暂停自动轮播并显示中文电量页（顶部红字「低电量 请充电」）；回升到 ≥15%（`LOW_BATTERY_EXIT`）自动恢复。
- **充电中一律不做低电量处理**：不弹电量页、不暂停轮播，插电瞬间若电量页正显示会立即清掉并放行。
  > 这里是踩过的坑：早期只在“重显提醒”那一支加了 `!charging`，而“首次进入低电量”那一支没有，于是**开机后第一次检查（最多 60s）时即使正在充电也会无条件刷出电量页**，把用户刚切换/上传的图片覆盖掉。现在把整个低电量分支都放在 `!charging` 之下。
- 低电量暂停**不写入 NVS**，所以断电重启后仍按用户保存的自动轮播开关执行。
- BOOT 双击可随时手动查看电量页（与低电量状态无关）。

**存储卡异常降级**

- SD 卡**挂载失败**（缺失/损坏/接触不良）时不再终止初始化：`User_Mode_init()` 只记日志并继续，否则 EventGroup、按键任务、`EPD_Init()`、各模式全都起不来（表现为开机无反应、热点也搜不到）。
- 相框模式下会在墨水屏给出可见提示页，而不是静默停在旧画面：
  - 卡未挂载 → 红字「存储卡未检测到 / 请检查存储卡」
  - 卡正常但目录为空 → 「未找到图片 / 请上传图片」
  - 列表有文件但读不出（运行中拔卡、文件损坏、非 24bit BMP）→ 红字「图片读取失败 / 请检查存储卡」
- 只要 `photo_img_count == 0`，开机就会主动刷一页提示；**有图时行为不变**（不额外刷屏）。
- 卡异常时 WiFi/AP 与 Web 仪表盘照常可用，方便排查；上传会因 `sdmmc_get_status()` 自检失败返回 HTTP 500。
- 状态页用字由 `tools/gen_battery_font.py` 生成到 `fontBatteryCN.c`，**加字后需重跑该脚本再编译**。

**FAT32 / 上传**

- **`readdir()` 顺序 ≠ 文件创建顺序**：上传后必须按文件名遍历列表找索引，不能假设新文件在列表末尾（`photo_web_server.cpp` 已修复）。
- **缩略图无限递归**：`calibrateThumb()` 末尾 `img.src = cv.toDataURL()` 会触发 `onload` 重入自身 → 无限循环 + 海量请求攻击 ESP32，必须在 `onload` 开头 `img.onload = null` 切断。
- **HTTP 与缩略图竞争**：ESP32 单线程，顺序加载缩略图会阻塞上传；上传时调 `stopThumbs()` 暂停（`img.src=''` 中止当前请求 + `thumbPaused=1`）。
- **`Connection: close` 陷阱**：频繁强关 TCP 会让 lwIP 套接字陷入 TIME_WAIT（120 秒超时），10 张缩略图即可耗尽连接；依赖 HTTP keep-alive。
- 图片处理在**浏览器端**完成，ESP32 只落盘 + 刷新，不占算力。

---

## 相关项目

- `../AI-Photo-Picker/` — PC 端 VLM 智能选片 + 6 色渲染 + 一键推送（本固件的最佳拍档）
- `../ESP32-S3-PhotoPainter-新版/` — 微雪官方新版源码参考
- `../ConverTo6c_bmp-7.3/` — 桌面端 6 色 BMP 转换工具
- `../PhotoPainter-E-Ink-Spectra-6-image-converter/` — 6 色调色工具
- `../InkTime/` — AI 回忆相框（参考项目）
