<!-- 感谢提交！请把下面填完整，尤其是「测试情况」——这个项目大量问题是硬件/时序相关的，没实机验证的改动需要标注清楚。 -->

## 改了什么

<!-- 一句话说明；如果是修 bug，请写清根因，而不只是现象 -->

关联 issue：#

## 变更类型

- [ ] Bug 修复
- [ ] 新功能
- [ ] 重构 / 清理
- [ ] 文档
- [ ] 构建 / 工具链

## 影响范围

- [ ] 固件（`ESP32-S3-6Color-PhotoFrame/`）
- [ ] Web 仪表盘（`dashboard.html`）
- [ ] HTTP API（`photo_web_server.cpp`）
- [ ] 墨水屏驱动 / 字库（`port_bsp/`）
- [ ] PC 端工具（`AI-Photo-Picker/`、`ConverTo6c_bmp-7.3/`）
- [ ] 仅文档

## 测试情况

- [ ] `idf.py build` / `build_demo.ps1` **编译通过**（请附最后几行输出）
- [ ] **实机验证**过（说明：哪个模式、SD 卡正常还是异常、是否插着充电器）
- [ ] 只做了静态检查，**未实机验证**（请说明原因，例如手头没有板子）

## 自查清单

- [ ] 没有提交 `settings.json` / `photos.db` / `filelist.txt` / `logs/`（含 API Key 与个人相册路径）
- [ ] 没有提交构建产物：`build/`、`managed_components/`、`*.bin`
- [ ] 改了 `dashboard.html` 后**已重新构建**（`photo_web_pages.h` 由 `html_to_header.py` 自动生成，**不要手改**）
- [ ] 新增/修改了墨水屏上的中文提示文字时，已重跑 `tools/gen_battery_font.py` 重建字库
- [ ] 没有改 NVS 键名拼写 `PhotPainterMode`（少一个 `o` 是历史遗留，改了会导致老设备设置丢失）
- [ ] 涉及 SD 卡路径时，考虑了 `sdcard_name[128]` 的**全路径长度上限**（分类目录会占用额度）
- [ ] 涉及文件写入/删除后，走的是 `photo_rescan_current_dir()`，没有绕开列表刷新
- [ ] 新增 HTTP 接口时，更新了 `ESP32-S3-6Color-PhotoFrame/README.md` 的 API 表

## 备注 / 截图

<!-- 墨水屏相关改动请贴一张上屏效果图；仪表盘改动请贴截图 -->
