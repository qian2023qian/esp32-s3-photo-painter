# WebUI 重设计（设计稿）

本目录只放设计稿，**没有改动任何固件源码**。

| 文件 | 说明 |
|------|------|
| **`v2-light-print.html`** | **推荐方向** —— 浅色「纸张 / 印刷」风格，PC + 平板 + 手机三档响应式 |
| `v2-preview.png` | v2 预览：PC（状态 / 图片）+ 手机五页 |
| `webui-mockup.html` | v1（早期稿）—— 深色、仅移动端；保留仅作对比 |
| `preview.png` | v1 预览 |

单文件、自包含、无需构建，**双击即开**。

---

## v2：纸张 / 印刷（浅色）

![预览](v2-preview.png)

### 为什么换这套语言

旧版（以及 v1 设计稿）本质都是"深色卡片式后台"，配色与结构大同小异。v2 换一套
**印刷品**的语言，让界面和"墨水屏"这个介质本身呼应：

| 元素 | 做法 |
|------|------|
| 底色 | 暖白纸色 `#f7f5f0` + 极淡纸纹（避免大面积纯色的塑料感） |
| 标题 | **衬线**（Georgia / 宋体），编辑式排版；页头用一条黑色发丝线收口 |
| 分隔 | **发丝线 + 网格**，而不是圆角卡片 + 阴影 |
| 识别色 | 墨水屏 6 色做成**印刷色标**（顶栏/侧栏 logo、选中态） |
| 数据 | "规格表"式 2×4 网格，大号衬线数字 |
| 作品展示 | 像**展签**：白色卡纸 + 细边框 + 等宽编号（`006 / 058`） |
| 图片列表 | 像**接触印相样张**（contact sheet）：白底、细边、等宽序号 |
| 按钮 | 方正（2px 圆角），主按钮用墨色实心 —— 印刷感而非"App 感" |

### 响应式：同一套标记，三档切换

| 断点 | 版式 |
|------|------|
| **≥1024px（PC）** | 左侧栏导航（236px）+ 版心 1120px；状态页两栏、图片 6 列、设置两栏 |
| **641–1023px（平板）** | 顶部横向导航；图片 5 列；两栏内容 |
| **<641px（手机）** | **底部页签**；单列；图片 3 列；时/分步进器整行两个大控件 |

导航是**同一份 HTML**，只靠媒体查询改变 `position` / `flex-direction`，不重复标记。

### 做法与自检

- 打开后可点底部/侧边页签；也可用锚点直达：`#p-status` `#p-photos` `#p-upload` `#p-play` `#p-settings`
- **横向溢出是可测量的**（内置自检）：
  `?w=390&measure=1#p-upload` 会把"右边界超出参考宽度"的元素连同父级类名与 HTML 片段
  列在页面顶部；`?w=NNN` 用于模拟指定宽度（绕过无头浏览器的视口下限）。

  **实测结果**：

  | 档位 | 视口 | overflowCount | 横向滚动 |
  |------|------|---------------|----------|
  | PC | 1374 | **0** | 无（bodyScrollWidth = 1374） |
  | 平板 | 739 | **0** | 无 |
  | 手机 | 492（手机样式）| **0** | 无（模拟 390 时 bodyScrollWidth = 390） |

  自检过程中抓到并修掉两个**真实 bug**：

  1. **类名冲突**：照片网格与底部弹层都叫 `.sheet`，导致两条 CSS 规则互相污染——
     照片网格被套上 `position:fixed`、弹层被套上 `display:grid`。已把照片网格改名
     `.contact`。这个 bug 在 360px 下表现为照片单元宽 151px 撑到 477px。
  2. **grid 列不可收缩**：`.cols` 的隐式列是 `auto`，子项 `min-content` 把整列撑到
     375px（超出手机宽度 15px）。已改为显式 `minmax(0,1fr)` 并给子项 `min-width:0`。

### 与固件的对应（便于日后移植）

设计稿刻意**沿用固件里已有的元素 id**，移植时基本是"换皮 + 接线"：

| 设计稿元素 | 固件现有 id | 接口 |
|---|---|---|
| 顶栏/侧栏电量、温湿度 | `batt-tag` / `batt-volt` / `batt-chg` / `s-temp` / `s-rh` | `GET /api/status` |
| 正在显示（展签） | `preview-processed` | `GET /api/photo?name=…&thumb=1` |
| 接触印相网格 | `photo-list` | `GET /api/photos`（`?reload=1` 重扫） |
| 目录选择 / 新建目录 | `set-dir` / `new-dir` | `GET /api/dirs`、`POST /api/mkdir` |
| 轮播方式 | `set-mode` | `POST /api/settings {mode}`（0/1/2） |
| 轮播间隔（时 / 分） | `set-interval-h` / `set-interval-m` | `POST /api/settings {interval}`（**分钟**） |
| 自动轮播 | `set-running` | `POST /api/settings {running}` |
| 休眠时段 | `set-sleep-start` / `set-sleep-end` | `POST /api/settings` |
| 上传 / 调色 | `file-input`、`adj-*` | `POST /api/upload?name=…&show=` |
| WiFi 配网 | `wifi-ssid` / `wifi-pass` / `ssid-list` | `GET /api/wifi/scan`、`POST /api/wifi/*` |
| MQTT | `set-mqtt-*` | `POST /api/settings {mqtt}` |
| 重启 | — | `POST /api/reboot` |

### 关于体积：不是硬约束

`partitions/v2/16m.csv` 里的 `assets`（SPIFFS，**7552 KB**）因 `CONFIG_FLASH_NONE_ASSETS=y`
**从未被烧写、也没有挂载**。把它缩小（或删掉）之后，两个 OTA 槽各自可以从 4352 KB
扩到 **~7.4 MB** —— 100~300 KB 的 HTML 完全放得下。

**唯一与体积无关、必须保留的约束：不能上 CDN**（配网热点没有外网，页面必须自包含）。

### 尚未做

- **未接后端**，全部假数据；页签、开关、分段、步进器、弹层只是**演示交互**
- 未做浅色/深色切换（v2 就是浅色方案；如需深色，CSS 变量已集中，可再补一套）
- 未做"本次上传"逐项重试的完整交互、图片多选批量操作、缩略图懒加载占位
- 顶部的"设计稿 · 假数据"提示条正式移植时**要删掉**
- 未实测设备端：HTML 传输耗时、缩略图串行加载节奏、手机浏览器字体回退

## 建议的下一步

1. **先定 v2 这个方向**（配色 / 密度 / 文案你过一遍），只改这里，不动固件
2. 再把 `components/user_app_bsp/mode_src/dashboard.html` 换成新结构 ——
   `html_to_header.py → photo_web_pages.h` 的构建流程不变；
   **注意改完要确认 `photo_web_server.cpp` 真的被重新编译**（见固件 README 的构建说明）
3. 若 HTML 明显变大，顺手缩 `assets` 分区、扩 `ota_0` / `ota_1`
4. 实机校验：热点下手机/PC 打开、缩略图加载、批量上传进度、长按多选
