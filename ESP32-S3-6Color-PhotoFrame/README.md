# ESP32-S3 6-Color PhotoFrame

> 基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（微雪官方新版固件重构）的 **7.3 寸六色墨水屏智能相框**。

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
| 上传照片 | 选任意图片 → 实时预览原图 vs 墨水屏效果 → 旋转 →「显示到屏幕」 |
| 删除照片 | 图片列表点 `x` |
| 图片列表 | 分页（`pageStore` 缓存已访问页，翻回秒开）；缩略图 400px 降采样顺序加载 |
| 图像调节 | 亮度 / 对比度 / 饱和度 / 锐化 / 抖动矩阵 / 扩散强度（实时） |
| 双管线 | epdoptimize / OpenDisplay 一键切换，并排对比 |
| 轮播设置 | 修改切换间隔（分钟）＋ 自动轮播开关（**开关点一下就立即存入 NVS**，断电/重启后仍保持） |
| 休眠时段 | 设置开始 / 结束时间 |
| 重启 | 页面底部红色按钮 |

> 图片处理全部在**客户端浏览器**完成（Canvas 缩放 + 6 色抖动 → BMP），ESP32 端只负责写 SD 卡 + 刷新墨水屏，不消耗设备算力。

---

## 六色渲染管线

墨水屏支持 6 色（黑 / 白 / 黄 / 红 / 蓝 / 绿）。仪表盘内置两套专业抖动管线，把任意图片转换为可直接上屏的 6 色 BMP：

| 管线 | 引擎 | 说明 |
|------|------|------|
| `epdoptimize` | JavaScript | 默认，校准色板 + Lab 色差匹配 |
| `OpenDisplay` | WASM/Rust | 纯色板，加权 RGB/HWD 色彩方案 |

两者都支持图像预处理（亮度 / 对比度 / 饱和度 / 锐化）和多种误差扩散矩阵：**Floyd-Steinberg、Atkinson、Jarvis-Judice-Ninke、Stucki、Burkes、Sierra-3、Sierra-2**（可蛇形抖动）。

前端 bundle 由 ESP32 直接以 `/lib/epdoptimize.js`、`/lib/opendisplay.js` 提供（`components/user_app_bsp/mode_src/dashboard.html` 同源生成）。

---

## 上传文件名格式

上传后自动按参数缩写命名，ESP32 端按文件名反查索引，格式：

```
管线缩写_抖动缩写_亮度_对比度_饱和度_锐化_扩散_时间戳.bmp
```

例：`epd_fs_0_20_20_50_100_1764154920.bmp`

- 管线缩写：`epd`（epdoptimize）/ `od`（OpenDisplay）
- 抖动缩写：`fs` / `at` / `jjn` / `sk` / `bk` / `s3` / `s2`

> ⚠️ 文件列表缓冲区 `sdcard_name[128]`，文件名（含路径）不能超过 127 字符。

---

## HTTP API

由 `components/user_app_bsp/mode_src/photo_web_server.cpp` 提供（默认端口 80），已开启 CORS：

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/` | 仪表盘 HTML |
| GET | `/api/status` | 设备状态（WiFi、电量、传感器、图片、设置） |
| GET | `/api/photos` | 图片列表 |
| GET | `/api/photo` | 单张图片数据 |
| GET | `/api/wifi/scan` | WiFi 扫描 |
| POST | `/api/wifi/connect` | 连接 WiFi `{"ssid","password"}` |
| POST | `/api/wifi/reset` | 重置 WiFi |
| POST | `/api/upload` | 上传图片（raw BMP，`?name=` 指定） |
| POST | `/api/delete` | 删除图片 `{"name"}` |
| GET/POST | `/api/adjustments` | 读取 / 保存图像调节参数 |
| POST | `/api/settings` | 保存设置 `{"interval","running","sleep_start","sleep_end"}` |
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
- 轮播间隔单位是**分钟**（代码 `* 60 * 1000` 转毫秒）；小智的 `imgsetTimerloop` 才区分分/时。
- 图片切换有 **15 秒冷却**（`photo_switch_to` 用 `esp_timer_get_time()` 微秒内部时钟，不依赖 NTP 对时）。
- 墨水屏刷新受 `epaper_gui_semapHandle` 互斥/信号量保护；图片切换经 `epaper_groups` EventGroup 触发。

**设置持久化（断电保存）**

- 相框设置（`interval` / `running` / `sleep_start` / `sleep_end`）以**完整 JSON** 存在 NVS 命名空间 `photoframe` 的 `interval` 键里，开机时整体恢复。
- **自动轮播开关点一下即保存**：前端立刻 `POST /api/settings {running:...}`，不需要再点「保存设置」，断电或重启后状态保持。
- 服务端落盘调用 `photo_persist_settings()` 写**完整规范状态**而不是请求原文——否则只改 `running` 的局部更新会把 `interval` / `sleep_*` 一起覆盖掉。
- `photo_running` **只表示用户开关**（持久化）；低电量暂停是临时状态 `low_battery_active`，不写 NVS，充电恢复后按用户开关决定是否继续轮播。
- `interval` 在服务端钳制到 1~1440 分钟，与仪表盘输入框范围一致。

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
