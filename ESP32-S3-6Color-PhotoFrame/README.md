# ESP32-S3 PhotoPainter

基于 ESP32-S3 的智能墨水屏电子相框，**7.3 寸 Waveshare Spectra 6 六色墨水屏 (800×480)**。支持 SD 卡本地图片轮播、WiFi 配网传图、Web 仪表盘实时调色，以及 AI 语音交互和火山引擎 AI 图片生成。

基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 开源项目修改。

## 硬件

| 组件 | 型号 | 接口 |
|------|------|------|
| 主控 | ESP32-S3, 16MB Flash, Octal PSRAM | — |
| 屏幕 | Waveshare 7.3" Spectra 6 墨水屏 | SPI |
| 音频 DAC | ES8311 | I2S |
| 音频 ADC | ES7210 | I2S |
| 电源管理 | AXP2101 | I2C |
| 温湿度 | SHTC3 | I2C |
| 存储 | Micro SD 卡 | SDMMC |

**关键引脚**：见 `main/boards/waveshare-s3-PhotoPainter/config.h`

## 六色墨水屏

墨水屏支持 6 种颜色：**黑、白、黄、红、蓝、绿**。仪表盘内置两种专业抖动管线，将任意图片转换为墨水屏可用的 6 色 BMP：

| 管线 | 引擎 | 说明 |
|------|------|------|
| epdoptimize | JavaScript | 默认，140KB，LAB 色差匹配 |
| OpenDisplay | WASM | 200KB，Rust 编译，HWD 色彩方案 |

两种管线均支持图像预处理（亮度/对比度/饱和度/锐化）和多种抖动矩阵（Floyd-Steinberg、Atkinson、Jarvis-Judice-Ninke、Stucki、Burkes、Sierra-3、Sierra-2）。

## 工作模式

启动时根据 NVS `PhotPainterMode` 键值选择模式：

| 值 | 模式 | 说明 |
|----|------|------|
| 0x03 | 小智模式 | 默认，AI 语音交互 + AI 图片生成 |
| 0x02 | 网络模式 | WiFi 配网 + Web 仪表盘传图 |

## 项目结构

```
ESP32-S3-6Color-PhotoFrame/
├── main/boards/waveshare-s3-PhotoPainter/  # 开发板引脚配置
├── components/
│   ├── user_app_bsp/mode_src/
│   │   ├── dashboard.html          # ★ 仪表盘 UI（编辑这个）
│   │   ├── html_to_header.py       # 构建时 HTML→C++ 转换
│   │   ├── photo_web_pages.h       # 自动生成，勿手动编辑
│   │   ├── photo_web_server.cpp    # HTTP API 服务端
│   │   ├── PhotoFrame_mode.cpp     # 相框模式核心逻辑
│   │   └── xiaozhi_mode.cpp        # 小智 AI 模式
│   ├── epaper_src/ + epaper_port/  # 墨水屏驱动
│   ├── port_bsp/sdcard_bsp.*       # SD 卡驱动
│   └── audio_bsp/                  # 音频 I2S
├── managed_components/             # ESP-IDF 组件
├── ConverTo6c_bmp-7.3/             # 离线图片转换工具
└── 03 Firmware/                    # 预编译固件
```

## 编译

- **IDF 版本**: ESP-IDF v5.5.4
- **目标**: `esp32s3`

```powershell
# Windows PowerShell
.\build_demo.ps1
```

首次编译前确保已安装 ESP-IDF v5.5.x 并设置好环境变量。

## SD 卡准备

SD 卡中放入初始图片到 `/sdcard/photos/` 目录（800×480 BMP 格式，6 色设备色）。可使用 `ConverTo6c_bmp-7.3/convert.py` 提前转换。

## 仪表盘开发

仪表盘 HTML 已拆分为独立文件，修改流程：

1. 编辑 `components/user_app_bsp/mode_src/dashboard.html`
2. 运行 `build_demo.ps1`（自动重新生成 `.h` 并编译）
3. 烧录固件

**不要再直接编辑 `photo_web_pages.h`**——它会在每次构建时被 `html_to_header.py` 覆盖。

## 仪表盘功能

- 实时温湿度显示
- 图片列表（分页浏览、缩略图预览、点击切换）
- 上传新图片前实时预览墨水屏效果（校准色板/设备色）
- 亮度/对比度/饱和度/锐化/抖动矩阵/扩散强度 可调
- 双管线切换
- WiFi 配网（STA+AP 降级，AP: PhotoFrame / 12345678）
- 休眠时段设置
- 图片轮播间隔调节

## 上传文件名格式

```
管线缩写_抖动缩写_亮度_对比度_饱和度_锐化_扩散_时间戳.bmp
```

例：`epd_fs_0_20_20_50_100_1764154920.bmp`

管线缩写：`epd` (epdoptimize) / `od` (OpenDisplay)
抖动缩写：`fs` / `at` / `jjn` / `sk` / `bk` / `s3` / `s2`

## 注意事项

- NVS 命名空间键为 `PhotPainterMode`（少一个 o，不要修正拼写）
- SD 卡列表缓冲区 `sdcard_name[128]`，文件名含路径不超过 127 字符
- 墨水屏刷新受 `epaper_gui_semapHandle` 信号量保护
- 图片切换通过 `epaper_groups` EventGroup 位 0 触发
- 预编译固件 `03 Firmware/ESP32-S3-PhotoPainter-Fac.bin`
