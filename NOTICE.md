# 第三方组件与许可声明 / Third-Party Notices

本仓库是一个多项目工作区，除了自有代码（以 [`LICENSE`](LICENSE) 的 MIT 许可发布）之外，
还包含**第三方开源代码的衍生作品**与若干第三方组件、数据集。它们各自保留原有许可与
版权声明，**不**受本仓库 MIT 许可的重新授权。

## 1. 第三方代码的衍生作品

| 目录 | 来源 | 许可 |
|------|------|------|
| `ESP32-S3-6Color-PhotoFrame/` | 基于**微雪官方固件**（ESP32-S3-PhotoPainter）修改。该官方固件本身是 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的衍生——微雪例程目录即 `01_Example/xiaozhi-esp32`，其 README 亦注明"该项目使用了虾哥 xiaozhi-esp32 开源项目"。本固件的框架层（`main/`，含小智 AI 语音、LVGL、协议栈）与相框应用层（`components/`）分别来自这条链路 | xiaozhi-esp32 部分为 MIT，Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.（原许可见该目录下 `LICENSE`）；微雪官方代码部分见其发行包内声明 |
| `AI-Photo-Picker/` | 衍生自 upstream 的 MIT 项目 | MIT，Copyright (c) 2025 dai-hongtao（原许可见该目录下 `LICENSE`） |

## 2. 第三方组件

| 组件 | 用途 | 许可 |
|------|------|------|
| [ESP-IDF](https://github.com/espressif/esp-idf) 及其托管组件 | 固件框架 | 多数 Apache-2.0 / MIT。通过 `idf_component.yml` 在构建时拉取，**未**随本仓库分发（`managed_components/` 已在 `.gitignore` 中忽略） |
| [LVGL](https://lvgl.io/) | 图形库 | MIT |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | AXP2101 电源管理驱动 | MIT，Copyright (c) Lewis He |
| [multi_button](https://github.com/0x1abin/MultiButton) | 按键驱动 | MIT |
| 墨水屏驱动 / `epdoptimize` / `OpenDisplay` | 6 色渲染与点阵字库 | 见 `ESP32-S3-6Color-PhotoFrame/` 内各文件的头部声明 |

## 3. 数据集

| 数据 | 来源 | 许可 |
|------|------|------|
| `AI-Photo-Picker/data/world_cities_zh.csv` | [GeoNames](https://www.geonames.org/) 数据制作 | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) —— 使用时请保留 GeoNames 署名 |

## 4. 其他

- 作者本机另有一份 `参考/` 目录（微雪官方新版源码、InkTime 等），仅作本地对照，**未纳入本仓库**。
- `ConverTo6c_bmp-7.3/` 中原先随附的 Windows/Mac 预编译二进制因没有许可声明，已移出仓库；
  仓库内只保留 `convert.py` 实现。
- 固件中可能出现的第三方商标、产品名（Waveshare、小智 等）归各自所有者所有。
