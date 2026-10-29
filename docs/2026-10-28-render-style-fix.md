# 第二页黑屏与翻转/滑动样式修复（2026-10-28）

## 根因

性能采集和板端解析一直正常：真实日志持续出现 PERF ACK，`UI?` 也能读到八项标签文本。但通过新增的 LVGL flush-buffer 抓取发现，修复前第一页有 74 种颜色、548 个亮像素，第二页只有 29 种颜色、34 个亮像素；第二页除顶栏和页码外基本全黑。

`battery_font_12.c` 的 `lv_font_fmt_txt_dsc_t.bitmap_format` 为 1（压缩），`lv_conf.h` 的 `LV_USE_FONT_COMPRESSED` 为 0。LVGL 在这种组合下返回空字形位图，因此所有使用该字体的标签都无法绘制。第一页仍有设备名、百分比和电量条，是因为这些对象使用 Montserrat；第二页全部标签使用自定义字体，所以表现为整页黑屏。

## 修改

- 用 `lv_font_conv 1.5.3 --no-compress` 重新生成 12px Noto Sans SC 子集，保留 ASCII 和当前 49 个中文字符。最终 `bitmap_format=0`，不要求 LVGL 解压功能。
- `Test-FirmwareAssets.ps1` 同时检查字符覆盖和字体格式/解压配置；压缩字库配合关闭解压时直接失败。
- `BatteryDashboard.cpp` 使用不透明页面和卡片：电量页两张设备卡；监控页 2×4 指标卡，CPU/内存/GPU 加 3px 进度条；底部增加页码指示条。
- 横滑从隐藏/显示改为两个页面在 304px 视口内平移动画，180ms ease-out；动画结束统一归位。两方向均切换另一页，动画期间拒绝重复触发。
- 翻转前取消页面动画并归位，显示方向切换后执行 `lv_refr_now`，视口由 70% 不透明度在 140ms 内淡入，减少黑帧和残影。
- `FRAME?` 抓取 LVGL 实际 flush 像素为 160×120 RGB565 文本；`Get-PanelFrame.ps1` 转为 480×360 PNG；`Test-PanelRendering.ps1` 检查两页、两个方向、反复滑动、字体位图和非空像素。

## 验证

| 项目 | 结果 |
| --- | --- |
| 字体 | 49 个中文字符、118 个 glyph、缺字 0；`bitmap_format=0` / `LV_USE_FONT_COMPRESSED=0` 兼容检查通过 |
| 编译 | Arduino CLI `--warnings all` 通过；程序 1,234,672 字节（94%），全局变量 116,452 字节（35%）；仅保留 LVGL 上游告警 |
| 烧录 | COM12，应用镜像 1,236,992 字节，esptool 哈希校验通过并自动复位 |
| 修复前像素 | 第 1 页：74 colors / 548 bright；第 2 页：29 colors / 34 bright，第二页基本为空 |
| 修复后像素 | 第 1 页：94 colors / 607 bright；第 2 页：59 colors / 185 bright，两页均超过非空阈值 |
| 渲染内容 | 第二页抓取可见 CPU、内存、GPU、GPU TEMP、上传、下载、磁盘、运行八张卡片；CPU/内存/GPU 进度条可见 |
| 导航 | 正向和反向显示状态下，左右滑动画均能切页；连续 6 次切换后 `sliding=0`、`fontBitmap=ready`、`boundsErrors=0` |
| 翻转 | NORMAL / REVERSED / AUTO 和姿态策略自测通过；两种方向下监控页均有非空渲染，翻转不会清空性能状态 |
| 其他回归 | 13 个 PowerShell 脚本解析、字体、精确 ACK、电量字段、HTTP/token、主动刷新、设置清理通过；电量/刷新/独立过期和方向板端测试通过 |
| 真实数据 | 最终真实采集仍返回鼠标 63%、耳机 93%、GPU 温度约 52–58°C；最新后台继续严格 ACK、2 秒轮询 |

修复后的抓图位于本机会话临时目录：`%TEMP%\battery-after-page1.png`、`%TEMP%\battery-after-page2.png`。这些图来自实际 LVGL 输出像素，能证明固件确实绘制了内容；它们不等同 LCD 实物照片，无法验证玻璃屏幕色偏、触控手感或安装角度。

## 2026-10-29 拉取最新版后复测

按用户要求执行 `git pull --ff-only`，`master` 从 `0c4b7e0` fast-forward 到 `origin/master` / `4dcf9bb`，新增新版指标数据结构和 `docs/monitor-preview.html`。无本地未提交文件或冲突；新版本已实机重新编译、烧录、测试。

| 项目 | 结果 |
| --- | --- |
| 最新版编译与烧录 | Arduino CLI `--warnings all` 通过；程序 1,234,672 字节（94%），全局变量 116,452 字节（35%）；esptool COM12 镜像哈希校验通过 |
| 修复后的实际渲染 | 最新固件 PAGE 1 / PAGE 2 抓取到 94 色 / 607 亮像素与 59 色 / 185 亮像素；字库诊断 `fontBitmap=ready`。CPU、CPU TEMP、GPU、GPU TEMP、MEM、UP、DOWN 七项新版卡片全部有像素 |
| 动画与方向 | NORMAL/REVERSED 两种方向各抓取两页，6 次连续滑动后均 `sliding=0` / `boundsErrors=0`；LVGL 输出缓冲非空 |
| 硬件回归 | 电量 0/7/71/100/unknown、非法帧、BAT1 兼容、刷新完成/防抖/超时、独立过期通过；测试结束清除合成数据 |
| 数据传输 | 最新桥按 10 个 PERF 字段发送 CPU temperature 与 MEM 等指标，实机 ACK 路径通过；AMD PMLog GPU edge 温度正常 |
| 离线回归 | 13 个 PowerShell 脚本解析、AMD 读温度类编译、字体压缩兼容、数据解析/ACK/分包、HTTP/token 与清理通过 |

本轮生成的 LVGL 输出抓图：`C:\Users\20878\AppData\Local\Temp\battery-render-test-29d2fae9dee046b49d1195dd845a56c2`。抓图是放大三倍的 framebuffer 像素，不是 LCD 拍照。

## 当前运行

后台采集 PID `19260`，日志 `C:\Users\20878\AppData\Local\Temp\battery-panel-live-fixed-render.log`；USB 严格 ACK、2 秒轮询、自动方向诊断。当前页面停留在第二页，便于现场直接确认。未创建系统启动项。

停止采集以释放串口：

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\BatteryBridge.ps1 -Stop
```
