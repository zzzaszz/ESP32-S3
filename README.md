# 无线设备电量显示板

## 最新修正：第二页黑屏与滑动/翻转样式（2026-10-29 已拉取并复测）

- 黑屏根因不是性能数据缺失：`battery_font_12.c` 之前是压缩字库（`bitmap_format=1`），而 `lv_conf.h` 关闭了解压（`LV_USE_FONT_COMPRESSED=0`），所以第二页全部标签有文本、有坐标，但实际像素为空。当前字库重新生成为非压缩格式（`bitmap_format=0`），并加入构建期兼容性检查，防止再次出现“有数据但不画字”。
- 两页采用深海军蓝底色、蓝色描边和青绿状态色，设备状态与大号数值分层显示。监控页使用 CPU 使用、CPU 温度、GPU 使用、GPU 温度、跨双列内存和网络指标卡，使用率保留进度条。
- 左右横滑使用 180ms ease-out 位移动画，动画结束强制归位；连续滑动期间忽略重复触发，避免页面叠加、跳回或停在屏幕外。
- 翻转前终止滑动并将页面归位，切换 LCD 方向后强制整屏刷新，再用 140ms 淡入恢复，减少黑帧和残影。显示方向与触控仍同步。
- 新增实际绘制缓冲抓取和 PNG 转换。修复前第二页仅 29 种颜色、34 个亮像素；修复后为 59 种颜色、185 个亮像素，系统卡片和指标文字已真实输出。修复后的第一页为 94 种颜色、607 个亮像素。
- 2026-10-29 已从 `origin/master` fast-forward 到 `4dcf9bb` 并在 COM12 重新编译烧录。同步验证了新版 CPU 温度、内存等监控指标；渲染与电量回归均通过，后台采集已恢复。

详细根因、画面回归和运行状态见 [黑屏与交互样式修复记录](docs/2026-10-28-render-style-fix.md)。

## 最新修正：正反方向与系统信息页

- AUTO 正反映射已依据当前板卡的实物反馈反向校准，显示和触控仍同步翻转。`ORIENTATION INVERT OFF` 表示使用已修正的板卡默认方向；`ON` 是在此基础上再反向。手动 NORMAL / REVERSED 不变。
- 从电量页向左或向右滑都能进入 PC 监控，再次横滑返回。根屏幕和页面内容区均接入手势，事件处理后停止冒泡，避免一次滑动切换两次。
- `UI?` 现在打印当前页、`monitorVisible`、性能数据年龄和七项渲染文字。`PAGE 1/2`、`SWIPE LEFT/RIGHT` 可诊断导航和显示逻辑；串口命令自测不替代实物触摸验收。
- RX 6600 XT 温度从 AMD 驱动的只读 PMLog 接口获得，无需安装额外软件；无有效传感器时仍显示 `--`。页面温度明确是 **GPU 温度**，不冒充 CPU 温度。
- 2026-10-28 23:17 实际链路得到 CPU 18%、内存 55%、GPU 10%、GPU 温度 57°C，板端 `page=1, monitorVisible=1, boundsErrors=0`，两台设备同时真实更新。该组为真实查询，之前 42/68/77/65 为显示测试夹具。

详细修改和测试见 [正反映射与监控页测试记录](docs/2026-10-28-monitor-fix.md)。

基于 Waveshare ESP32-S3-Touch-LCD-2.8 的桌面电量显示项目，在 320 × 240 横屏上展示 HyperX 耳机和 Logitech 鼠标的电量、充电状态及数据更新时间。

## 自动识别与反转

默认使用板载 QMI8658 加速度计和陀螺仪识别两个横屏方向，倒置后自动翻转 180°，触控坐标同步变化。倾斜支架或竖起时同时支持 X/Y 主导的重力方向，稳定约 0.7 秒后切换；平放时累计陀螺仪相对转角，转到约 150° 时翻转以跟随半圈转动。传感器每 20ms 读取，静止噪声、剧烈晃动、触摸和数据中断会过滤。仍采用 320×240 横屏，不切换成竖屏布局。

平放的初始朝向以开机或最近一次竖起的方向为基准；六轴传感器没有磁力计，不能识别开机前的水平绝对朝向。水平转动时请避开触摸区持握；`ORIENTATION?` 的 `algorithm=accel+gyro` 可确认是支持平放转动的新版。

串口命令（先停止采集桥并关闭其他串口占用）：

```text
ORIENTATION?                 查询模式、方向、传感器状态、加速度（mg）及角速度/累计角度
ORIENTATION AUTO             自动识别（默认）
ORIENTATION NORMAL           固定原横屏方向
ORIENTATION REVERSED         固定反向横屏
ORIENTATION INVERT ON        将自动识别结果反向，适配安装方向
ORIENTATION INVERT OFF       取消自动方向反向修正
ORIENTATION TEST             运行方向判定及触控坐标自测，不替换真实传感器输入
```

模式和反向修正保存到 NVS，重启恢复；自动切换本身不反复写 Flash。传感器读取失败时保持方向并定期重试，手动固定方向仍可用。`INVERT` 只影响 AUTO，固定 NORMAL/REVERSED 不受它影响。实际搬动板卡后的图像和手势方向需要实物确认；详见 [方向识别测试记录](docs/2026-10-28-orientation.md)。

USB 连续采集可加 `-TraceOrientation` 同时记录方向、角速度和累计角度，不必再开串口监视器抢占 COM12。当前后台已开启该诊断；实际转动与画面朝向仍需实物验收。

## 2026-10-28 横屏与主动读取

- 固件版本 `BatteryPanel/2`；正文、标题和设备型号为 12px，电量为 16px。耳机、鼠标左右两列，PC 指标为两列四行。
- 已移除 `LOCK`、`MONITOR`、`BACK` 按钮。左右横滑均在电量页与 PC 监控页之间切换；两页均可下滑主动读取。
- 右上角仅保留 30 × 28px 刷新图标。点击后显示“正在读取电量”，两台设备回复后显示“已更新”；20 秒未收齐回复显示“采集程序未响应”。重复请求有 1.5 秒防抖。
- 必须运行本仓库新版 Windows 采集桥，主动读取才会查询真实设备。USB 通过 `REQUEST BATTERY <序号>` 通知采集桥；Wi-Fi 采集桥每秒查询 `/health` 的 `refresh` 序号。均会提前结束周期等待，重新查询 HID / G HUB，不在 LVGL 线程执行阻塞查询。
- 修复 ACK 前缀误匹配（7% 不再接受 71% 的回复），加强 G HUB 字段类型和设备 ID 校验，排除 HID 非当前 GET 通知。缺失字段维持未知，真实的 0% 保留；无效值、断开和过期显示 `--`。

连续运行采集（先关闭 Arduino 串口监视器；`-Once` 不会持续响应主动读取）：

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\BatteryBridge.ps1 -Port COM12 -RequireAck -IntervalSeconds 2 -LogPath "$env:TEMP\battery-panel-live.log"
# 另开终端停止，释放串口
powershell -ExecutionPolicy Bypass -File .\windows\BatteryBridge.ps1 -Stop
```

Wi-Fi 连续采集使用 `-PanelUrl http://battery-panel.local` 替代 `-Port COM12 -RequireAck`，保留已有 token 配置。串口独占时不能同时打开串口监视器。关闭采集桥后可发送 `REFRESH` 验证请求输出，发送 `UI?` 查看布局边界及渲染值；它们不替代物理触控验收。

详细步骤、测试证据及未完成事项见 [横屏与主动读取测试记录](docs/2026-10-28-landscape-refresh.md) 和 [后续姿态/采集验证](docs/2026-10-28-orientation.md)。20:37 实际收到耳机 GET 96%、鼠标 68%，两台均有板端 ACK；离线时仍显示未知。尚未与厂商界面同时对照验收精度，不以测试帧代替实际设备电量。

### 鼠标读取速度与断续显示

默认轮询已从 15 秒改为 2 秒，并从每轮开始计时。鼠标读到后立即发送，不再等待耳机及 PC 计数器；HID 路径枚举每 30 秒刷新，PC 指标每 15 秒采样。正常更新常见约 2 秒，首轮枚举、性能采样和超时会延长间隔。主动读取同样走提前查询路径。

本轮曾实际读到鼠标 `ACTIVE`、62%/未充电，并收到板端 `ACK BAT2 1 62`；之后 G HUB 也曾报告 `NOT_CONNECTED`、`ABSENT`。显示 `--` 时先查日志中的 G HUB 状态，不能仅凭接收器已插入就认定电量可读。停止采集或串口被占用超过 45 秒也会导致板端隐藏过期数据；烧录结束后需要恢复连续采集，`-Once` 仅发送一轮。

## 当前状态

项目仓库：[zzzaszz/ESP32-S3](https://github.com/zzzaszz/ESP32-S3)。当前本机仓库路径为 `D:\AgentForge\proj\ESP32-S3`，本轮基于提交 `c60e923` 修改。以下 2026-10-27 记录保留为历史证据，当前界面和操作以上方新版本说明为准。

仓库历史记录中此前已完成并验证（不代表本轮实机验收）：

- ESP32-S3 的 ST7789 屏幕初始化及 LVGL 英文界面。
- Windows 通过 G HUB 本地接口读取 PRO Wireless 鼠标电量。
- Windows 通过匹配型号的 HID 查询读取 HyperX Cloud III S Wireless 耳机电量。
- 两台设备的数据通过 USB 串口发送给 ESP32，板端分别返回确认。
- 双设备固件编译通过、烧录校验通过；各设备独立维护数据过期状态。

本轮已将该固件刻录到当前连接的 ESP32-S3（`COM12`，VID/PID `303A:1001`），刻录后程序镜像哈希校验通过，板端返回协议和数据 ACK。串口功能、帧验证和数据过期均已实机验证；鼠标和耳机均已读到有效电量并获得板端 ACK。屏幕文字、触控和 Wi-Fi 仍需目视/交互验收。

### 当前支持设备

当前采集桥只识别下表中的设备和显示主机，不会把任意蓝牙电池设备或相似型号自动当作受支持设备：

| 设备 | 识别方式 | 当前支持内容 | 协议编号 / 已记录结果 |
| --- | --- | --- | --- |
| HyperX Cloud III S Wireless | USB 2.4G 接收器 `03F0:02CC`，HID GET 查询 | 电量、充电状态、连接状态；异常时发送未知值 | `BAT2 device=0`；20:37 GET 返回 96%/未充电，离线曾返回 255 |
| Logitech PRO Wireless | G HUB 本地 WebSocket，匹配 `deviceModel=pro_wireless_mouse` | 电量、连接状态；G HUB 没有充电字段时使用 `charging=-1` | `BAT2 device=1`；在线实测 62%/未充电，也观察到 `NOT_CONNECTED` / `ABSENT` |
| Waveshare ESP32-S3-Touch-LCD-2.8 | ST7789 原生 240 × 320，当前旋转为 320 × 240，目标 FQBN `esp32:esp32:esp32s3:CDCOnBoot=cdc` | 接收 `BAT1`/`BAT2`/`PERF`，USB 串口和 Wi-Fi HTTP，滑动切换电量/PC 监控页、主动读取 | 已烧录 `BatteryPanel/2`；板端布局边界为 0；触控方向、屏幕观感待实物操作 |

暂不支持：其他 HyperX 型号、其他 Logitech 鼠标、直接从 ESP32 BLE 电池服务读取，以及未经适配的第三方接收器。当前方案依赖 Windows 访问专用接收器或 G HUB；电脑关机后不得继续把旧数值标成实时电量。

2026-10-28 两台设备在线时均已获得实际电量，耳机先前曾返回 `255`，鼠标也曾离线；显示按每次真实查询更新。71% / 98% 是此前合成协议测试值，不能作为真实电量证据。采集桥真实发送当前状态，不伪造百分比。Wi-Fi 未配置，故未完成无线全链路验收。

### 当前验收边界

| 证据 | 已覆盖 | 不能替代 |
| --- | --- | --- |
| `Run-OfflineChecks.ps1` | 11 个 PowerShell 脚本解析、字体 glyph 覆盖、电量解析/ACK 回归、9 个参数拒绝、token 请求头、两条 `BAT2` 帧和一条 `PERF` 帧、首条 HTTP 500 后继续发送、transcript 不泄露 token、PID/停止文件清理 | 触控、屏幕显示、真实 Wi-Fi 和设备电量 |
| `Run-OfflineChecks.ps1 -BuildFirmware` | 目标 FQBN 的 `WarningsAll` 固件编译和程序/RAM 体积 | 触控、屏幕目视检查、真实 Wi-Fi 和长时间运行 |
| `arduino-cli board list` 与设备管理器 | `COM12`，USB VID/PID `303A:1001`；esptool 确认 ESP32-S3，240 MHz，8 MB PSRAM | 不能替代屏幕目视、触控操作或 Wi-Fi 验收 |

临时 smoke 日志默认在脚本结束时删除；需要长期保留采集过程时，显式给 `BatteryBridge.ps1` 传入 `-LogPath`，并只记录不含 token 的 transcript。

### 本次自测与待优化点

2026-10-27 在本机运行 `windows/Run-OfflineChecks.ps1 -BuildFirmware`，并烧录当前板卡。结果如下：

- 7 个 PowerShell 脚本解析通过；页面中文字符 33 个，字体缺字 0。
- 参数拒绝测试 9 个用例通过，`BAT2` 和 `PERF` ACK 文本派生 3 条通过。本机设备已连接时曾令旧 smoke test 误读真实设备，现增加仅允许 `-Once` + loopback HTTP 的 `-OfflineTestMode` 固定测试夹具，之后离线完整测试通过。
- HTTP smoke test 验证了带 token、不带 token、两条 BAT2 帧加一条 PERF 帧，以及首条请求返回 HTTP 500 后继续发送后续帧；PID、停止文件和临时日志均完成清理。
- Arduino CLI `--warnings all` 固件构建通过：程序 `1,232,440` 字节（94%），全局变量 `116,316` 字节（35%）；LVGL 8.3.10 上游有枚举和 const 警告。镜像烧录到 `COM12`，esptool 哈希校验通过。
- 板端返回 `VERSION BatteryPanel/1`、`BRIGHTNESS 50`、`TOKEN NOT CONFIGURED`；HEALTH 确认 Wi-Fi disconnected。注入测试帧获得 `ACK BAT2 1 71`、`ACK BAT2 0 98`、`ACK PERF 42 68 77 65`，非法帧 `BAT2,9,999,1,0` 返回 `ERR FRAME`。71/98 是合成协议测试值，只用于确认显示数据接收链路，不是设备读数。
- 两次真实 `BatteryBridge.ps1 -Port COM12 -Once -RequireAck` 成功结束且等待 ACK 未超时。桥接器显示鼠标和耳机均未知：G HUB 当前未运行，耳机 HID 查询返回 `255`；没有有效实测电量。
- 手工重新发送单设备数据后等待 47 秒，`HEALTH?` 显示对应设备 `expired`、年龄 47 秒，确认板端 45 秒数据过期逻辑。另一设备此前已过期。
- 通过板端串口命令查询到的健康、版本和亮度数据正常。未目视检查屏幕/中文/颜色/裁切，未操作触屏，未配置 Wi-Fi，也未完成 2 小时长稳测试。

仍待完成的实物验收按优先级排列：

1. 目视检查横屏中文、电量条、颜色、裁切和更新时间；确认板背标签，并实测左右滑切页、下滑读取、右上角刷新和触控方向。固件检测到 `CST3530 READY`，标签仍以实物为准。
2. 保持 G HUB 运行、接通耳机接收器并确保鼠标在线，重跑 `BatteryBridge.ps1 -RequireAck`，将板端显示与 G HUB/NGENUITY/耳机报告的同时刻电量交叉核对。耳机 GET 返回 `255` 时只能标记未知。
3. 配置可用的 2.4 GHz Wi-Fi 后测试 token、`/health`、mDNS、断网重连和 USB 数据线断开后的持续更新。当前没有网络凭据，板端报告 Wi-Fi disconnected。
4. 进行至少 2 小时 USB/Wi-Fi 连续运行与断开重连测试，观察堆内存、请求超时、数据年龄和串口占用。
5. 如能提供可用相机视角或实机操作记录，补充目标屏幕截图/像素级回归，自动发现中文缺字、长型号溢出、`100%` 重叠和状态刷新跳动。

## 架构与边界

```text
HyperX 2.4G USB 接收器 → Windows HID 查询 ┐
                                         ├→ Windows 采集程序 → USB / Wi-Fi → ESP32 → LVGL
Logitech USB 接收器 → Windows G HUB 接口 ┘
```

这两台设备当前使用专用 2.4G 接收器，不能通过 ESP32 标准 BLE 电池服务直接读取。现阶段依赖 Windows 运行采集程序；电脑关机后不得继续把旧数值标成实时电量。USB 是已验证的传输方式，Wi-Fi HTTP 接口此前已实现，但尚未完成联网实测。

## 硬件与环境

| 项目 | 配置 |
| --- | --- |
| 板卡 | Waveshare ESP32-S3-Touch-LCD-2.8，16 MB Flash、8 MB PSRAM |
| 屏幕 | ST7789，原生 240 × 320，当前 320 × 240 横向 |
| 触控 | V1 为 CST328；V2 为 CST3530；按背面标签自动探测；当前固件已接入统一 LVGL 指针输入 |
| Arduino IDE | `E:\ArduinoIDE\Arduino IDE\Arduino IDE.exe` |
| Arduino CLI | IDE 安装目录下 `resources\app\lib\backend\resources\arduino-cli.exe` |
| 已验证板卡包 | Espressif ESP32 `3.3.10-cn` |
| 图形库 | LVGL `8.3.10` |
| 构建目标 | `esp32:esp32:esp32s3:CDCOnBoot=cdc` |
| 测试串口 | 本轮确认 `COM12`（USB `VID_303A&PID_1001`），115200；重新插拔后仍需重新确认端口 |

官方示例中的屏幕引脚：MOSI 45、SCLK 40、CS 42、DC 41、RST 39、背光 5。屏幕两版本都使用 ST7789；触控控制器不同，不能只替换一个型号字符串后复用驱动。

### V1/V2 触摸屏代码兼容说明

官方提示：ESP32-S3-Touch-LCD-2.8 V1 已停产，2026-06-05 之后出货的产品切换为 V2。两种版本都使用 ST7789 显示控制器，但触控芯片不同：

| 硬件版本 | 显示控制器 | 触控控制器 | 版本确认方式 | 当前代码状态 |
| --- | --- | --- | --- | --- |
| V1 | ST7789 | CST328 | 产品背面标签 | 显示和触控驱动已接入，实机待验证 |
| V2 | ST7789 | CST3530 | 产品背面标签 | 显示和触控驱动已接入，实机待验证 |

兼容边界和接入规则：

- 必须以产品背面标签为准判断 V1/V2；出货日期只能作为提示，不能替代实物确认。
- `Display_ST7789.cpp`、LVGL 页面和电量传输代码与触控型号无关，V1/V2 可以共用显示部分。
- CST328 与 CST3530 的初始化寄存器、坐标报告和中断行为不能假设相同。错误地使用另一版本驱动，可能表现为无触控、坐标错乱或初始化失败。
- 后续代码应将 `CST328`、`CST3530` 做成两个独立驱动，由一个明确的硬件版本配置只选择其中一个，再通过统一的 `lv_indev_drv_t` 接口接入 LVGL；不能同时注册两个触控驱动，也不能运行时盲猜型号。
- 当前分支已加入官方 CST328/CST3530 的启动探测、I2C 读取和 LVGL 指针输入注册，并在没有触控芯片时降级为无触控显示；本轮已烧录实机，但没有捕获触控初始化日志、屏幕图像或触控操作结果，不能据此认定显示和触控验收通过。

当前构建基线为 Arduino ESP32 core `3.3.10-cn`、LVGL `8.3.10`、FQBN `esp32:esp32:esp32s3:CDCOnBoot=cdc`。V2 触控适配需要以官方 CST3530 示例使用的驱动接口和库版本为准，接入后必须重新编译，并在实物上验证触控坐标、显示方向、滑动和复位稳定性。

截至 2026-10-27，本机设备管理器枚举到 `COM12`（USB `VID_303A&PID_1001`），esptool 实际识别 ESP32-S3 并完成固件烧录与哈希校验。本轮串口协议、ACK、非法帧拒绝和过期状态已验收；屏幕目视、触控及真实 Wi-Fi 验收仍未完成。

## 源码职责

当前源码结构：

| 文件 | 职责 |
| --- | --- |
| `ESP32-S3-Touch-LCD-2.8-Battery-Display.ino` | 初始化、主循环 |
| `BatteryDashboard.cpp/.h` | LVGL 页面、设备行、状态更新 |
| `Touch_Driver.cpp/.h` | 官方 CST328/CST3530 自动探测、I2C 触点读取和 LVGL 指针输入 |
| `DisplayOrientation.cpp/.h`、`OrientationPolicy.h` | QMI8658 姿态读取、方向稳定判定、显示/触控同步翻转、模式持久化和板端自测 |
| `battery_font_12.c`、`fonts/NotoSansSC-LICENSE.txt` | 当前 12px 中文/ASCII 子集字体及其 OFL-1.1 许可；旧 `battery_font_16.c` 保留但页面不再引用 |
| `BatteryTransport.cpp/.h` | USB/HTTP 数据帧校验、`BAT1`/`BAT2`/`PERF` 解析、Wi-Fi 重连/mDNS、每设备过期处理 |
| `Display_ST7789.cpp/.h` | 官方屏幕引脚与初始化 |
| `LVGL_Driver.cpp/.h`、`lv_conf.h` | 显示缓冲、时钟、字体配置 |
| `windows/BatteryBridge.ps1` | 周期采集两台设备并发送数据 |
| `windows/Build-Firmware.ps1` | 自动发现 Arduino CLI，在唯一临时草图目录编译固件 |
| `windows/Test-BatteryBridge.ps1` | 本机 HTTP smoke test，验证两条 BAT2 帧、一条 PERF 帧和可选 token 请求头；`-ValidationOnly` 验证参数拒绝 |
| `windows/Test-FirmwareAssets.ps1` | 自动核对页面中文字符与 LVGL 子集字体 glyph 覆盖 |
| `windows/PanelProtocol.ps1` | USB 分包请求解析、精确 ACK、电量字段/设备身份校验 |
| `windows/Test-PanelHardware.ps1` | 板端合成帧、渲染值、刷新防抖/完成/超时、独立过期回归；结束清除测试电量 |
| `windows/Test-PanelRefresh.ps1` | 本地模拟面板、真实只读设备查询，验证连续采集被主动读取提前唤醒 |
| `windows/Test-PanelOrientation.ps1` | 实机方向策略自测、固定方向/反向修正命令和真实传感器读取验证；结束恢复 AUTO、INVERT OFF |
| `windows/Run-OfflineChecks.ps1` | 一键运行 PowerShell 解析、字体覆盖、桥接器验证、HTTP smoke test 和退出清理检查；加 `-BuildFirmware` 才执行固件构建 |
| `windows/Set-PanelToken.ps1` | 从进程环境变量读取 token 并通过 USB 配置面板 |
| `windows/Read-HyperX.ps1`、`HyperXReader.cs` | 匹配耳机 HID 接口、查询电量和充电状态 |
| `BLEProbe/BLEProbe.ino` | 独立 BLE 探测草图，非主显示固件 |

LVGL 页面放在普通 `.cpp` 中，主 `.ino` 只保留初始化和循环，避免 Arduino 自动生成函数原型时出现 `lv_obj_t` 尚未声明的问题。

## 采集与传输

鼠标通过 `ws://127.0.0.1:9010` 的 `json` 子协议，先 GET `/devices/list` 查找 `deviceModel=pro_wireless_mouse` 的活动设备，再 GET `/battery/{id}/state` 读取电量。不要硬编码运行时设备编号。

耳机只匹配 `03F0:02CC`。64 字节 Output Report 查询格式为 `0C 02 03 01 00 <command> 00 ...`，其中 `06` 查询百分比，`48` 查询充电状态。响应 `0C` 的第 5 字节为命令、第 6 字节为值（下标从 0 开始）。充电状态 `0` 为未充电、`1` 为充电、`2` 为充满；`FF` 和不合法值不得显示为电量。请求和响应可能属于不同 HID collection，需要读取匹配设备的各 collection。不得混用其他 HyperX 型号的协议或发送设置命令。

协议依据：

- [HyperHeadset 型号实现](https://github.com/LennardKittner/HyperHeadset/blob/main/src/devices/cloud_iii_s_wireless.rs)
- [HaloBattery Cloud III S 实现与说明](https://github.com/HeyOkay/HaloBattery/blob/main/providers/hyperx_cloud3s.py)
- [Cloud III S Windows Output Report 适配记录](https://github.com/HeyOkay/HaloBattery/pull/123)

双设备数据帧：`BAT2,device,level,connected,charging`，串口以换行结束。电脑性能帧为 `PERF,cpu,memory,gpu,gpuTemp,upKbps,downKbps,diskFreeGb,uptimeSec,cpuTemp`；CPU 温度字段追加在末尾，旧格式帧仍可解析，缺失温度显示 `--`。PC 监控页按 CPU 使用、CPU 温度、GPU 使用、GPU 温度、跨双列内存、网络上传/下载排列，不显示磁盘和运行时间；找不到明确标记 CPU/Processor/Package/Core 的 Windows ACPI 热区时保持未知，不以主板或 GPU 温度代替。时钟帧为 `TIME,<unix秒>`（UTC epoch），由 USB 采集桥每 30 秒推送一次，固件按 CST-8 显示；Wi-Fi 连接时 NTP 仍可独立校准，两种来源互不冲突。板载无 RTC 电池，重启后需等待下一条 TIME 帧才会恢复时钟。

| 字段 | 含义 |
| --- | --- |
| `device` | `0` 耳机，`1` 鼠标 |
| `level` | `0–100`，`-1` 未知 |
| `connected` | `0/1`，表示本次采集是否确认可用，不能仅凭接收器插入判定耳机在线 |
| `charging` | `0/1`，`-1` 未知；不把未知解释为未充电 |

板端返回 `ACK BAT2 <device> <level>`；保留 `BAT1,level,connected,charging` 旧鼠标帧兼容。默认每 15 秒采集一次，可通过 `-IntervalSeconds` 设为 1–3600 秒；每台设备独立超过 45 秒无新数据时隐藏旧百分比并显示过期。

## 后续任务：分步实施

按下面顺序开发，每一步验收后再进入下一步。复选框表示待办，不能以“编译通过”代替界面或业务验收。

### 第 1 步：确认源码与建立基线

- [x] 从 GitHub 重新拉取完整项目，确认耳机、鼠标及双设备传输源码存在。
- [x] 已在当前仓库临时 Arduino 草图目录重新构建，并通过 `Run-OfflineChecks.ps1 -BuildFirmware`：脚本解析、字体覆盖、9 个参数拒绝、HTTP smoke test 和 `WarningsAll` 构建通过；smoke 测试使用 loopback-only `-OfflineTestMode`，不会读取真实 HID/G HUB。
- [x] 已从当前仓库 `D:\AgentForge\proj\ESP32-S3` 调用离线检查，9 个拒绝用例、3 组 ACK 推导、token 和 HTTP 错误路径均通过。
- [x] 已检查分支、未提交修改、Arduino 板卡包和 LVGL 版本：`master`、ESP32 core `3.3.10`、LVGL `8.3.10`；现有改动已保留。
- [x] 编译并烧录当前 ESP32-S3；板端实测接受两条 BAT2 帧和 PERF 帧，返回预期 ACK；非法帧返回 `ERR FRAME`。测试电量 71/98 为合成协议值，不是设备读数。
- [x] 验证 Windows 桥 `-RequireAck -Once` 可与 COM12 建立串口并等待 ACK 正常结束；由于 G HUB 未运行且耳机查询返回 255，此轮真实电量帧是未知值。
- [ ] 对照实物确认横屏页面、滑动切页和主动读取，不只检查串口。

验收：工程能在本机重新构建，耳机与鼠标分别更新，失败时显示未知；不出现固定演示电量。

### 第 2 步：建立中文字体

- [x] 自动提取 `BatteryDashboard.cpp` 中文字符并核对 glyph 覆盖；当前 49 个不同中文字符，缺字数为 0。
- [x] 改用 12px 中文正文/标题字体；型号使用 Montserrat 12，百分比使用 Montserrat 16。
- [x] 当前字体由 `lv_font_conv 1.5.3` 从本机 `C:\Windows\Fonts\NotoSansSC-VF.ttf` 生成，包含 ASCII 0x20–0x7E，保留 OFL-1.1 许可。中文文案变更后必须重新生成并运行字体覆盖检查。
- [x] Arduino CLI `--warnings all` 构建通过；体积和最终烧录结果记录在本次测试文档。

验收：所有目标中文、英文型号和 `0% / 100% / --` 均正常显示；固件仍可编译、烧录，无缺字。

### 第 3 步：页面中文化

- [x] 标题改为“设备电量”，移除页面内开发说明和协议说明。
- [x] 状态统一为“等待数据”“已连接”“充电中”“充电状态未知”“暂不可用”“数据已过期”。
- [x] 保留产品型号“HyperX Cloud III S”和“PRO Wireless”，长型号允许省略；状态、标题、更新时间采用中文。
- [x] 显示“最近更新”或数据年龄；只有收到真实数据后才标为实时。

验收：正常、未知、未充电、充电、采集失败、过期六类状态文字完整，且不会把“未知”显示成“0%”。

### 第 4 步：调整 320 × 240 横向布局

当前采用两个设备并排布局，不为尚未接入的设备占位：

| 区域 | 建议位置/高度 | 内容 |
| --- | --- | --- |
| 标题栏 | y=2–29 | 页面名称、USB/Wi-Fi 状态、小刷新图标 |
| 耳机列 | x=8，y=40，148 × 162px | 型号、类型、百分比、电量条、状态、年龄 |
| 鼠标列 | x=164，y=40，148 × 162px | 型号、类型、百分比、电量条、状态、年龄 |
| 底部状态区 | y=218 | 读取反馈和页码 |

- [x] ST7789 设置 MADCTL `0x60`，LVGL 和显示驱动同步改为 320 × 240；触控原始坐标映射为 `x=rawY, y=239-rawX`。
- [x] 左右边距 8px，列间距 8px；名称、电量、状态各自固定区域，超长使用省略号。
- [x] 去除大图标和大导航按钮，设备百分比使用 16px，其余主要文字 12px。
- [x] 电量条固定为 142 × 6px，更新只改数值和颜色，不重复创建控件。
- [x] 正常、低电量、充电使用不同颜色；未知和过期隐藏数值，同时使用文字区分状态。
- [x] 板端 `UI?` 实测控件边界 `boundsErrors=0`；最终像素、旋转方向和可读性仍需实物确认。

验收：实机检查两设备名称、`100%`、长中文状态；无越界、重叠、裁字和更新跳动，30 分钟无明显闪烁。

### 第 5 步：交互与屏幕设置

- [x] 保留 CST328/CST3530 自动探测和 LVGL 指针输入；移除 `MONITOR`、`LOCK`、`BACK`，改为左右滑切页；性能页显示 CPU 使用、CPU 温度、GPU 使用、GPU 温度、跨双列内存和网络，移除磁盘与运行时间。
- [ ] 实机核对产品背面标签，确认横屏触控坐标、左右切页、下滑读取和小刷新按钮；V2 使用 CST3530，V1 使用 CST328。
- [x] 增加串口亮度设置 `BRIGHTNESS 0-100` / 查询 `BRIGHTNESS?`，设置写入 ESP32 NVS，开机恢复；输入 `HELP` 或 `?` 可查看 Wi-Fi、token、亮度、版本、健康状态和数据帧命令，`VERSION?` 返回当前协议版本；熄屏/唤醒仍待触控版本确认。
- [x] 新增手动读取及读取中/更新/超时反馈，通过序号通知 Windows 提前查询，LVGL 线程不做阻塞 HID / G HUB 调用。
- [x] 在硬件版本和驱动未验证前，显示页面和数据传输可独立运行；触控驱动与坐标验收仍待实机。

验收：触控坐标与显示方向一致，亮度范围有效，操作不造成卡顿或复位。

### 第 6 步：完成 Wi-Fi 联调

- [ ] 选择同一局域网的 2.4 GHz Wi-Fi；电脑可使用有线网络，但必须能访问 ESP32。
- [x] 支持通过 USB 串口发送 `WIFI<TAB>SSID<TAB>PASSWORD` 配置网络；校验 SSID 不超过 32 字节、密码不超过 63 字节，格式错误或超长串口行会返回错误；凭据不写入源码或固件日志，由 ESP32 Wi-Fi 配置持久化。
- [x] `/health` 返回 Wi-Fi/IP/mDNS 及 `device0/device1` 的 `waiting/fresh/expired`、`age0/age1`，串口 `HEALTH?` 返回同一状态；POST `/battery` 通过 raw 回调最多缓存 64 字节，非法 `Content-Length` 返回 400，超长或未授权请求不复制为完整帧，并由路由回调返回 413/403；非 raw 请求返回 400，不复用上一请求的帧。ESP32 `WebServer` 仍会读取网络 body 直到请求结束，因此这限制的是固件缓存和解析，不是网络读取时长；真实 Wi-Fi 联调仍待实机。
- [x] 加入断线后的周期重连和 DHCP 地址变化处理；通过 `battery-panel.local` 提供稳定主机名，串口 `IP` 命令保留作诊断。
- [ ] 脱离电脑 USB 数据连接，使用独立 USB 电源测试。此时仍依赖 Windows 在线采集。
- [x] 支持可选的 32 字符十六进制（128 位）token：通过 `Set-PanelToken.ps1` 写入 NVS，HTTP 请求使用 `X-Battery-Token`；串口发送 `TOKEN<TAB>CLEAR` 可清除 token，`TOKEN?` 会区分已配置、未配置和 NVS 中的无效 token；未配置 token 时只适合 USB/可信局域网开发。
- [ ] 在可信局域网内实测 token 拒绝/放行和访问边界；不要将未认证的 HTTP 接口暴露到公网。

验收：拔掉 USB 数据连接后两设备继续更新；断网超过 45 秒显示过期，联网后自动恢复。

### 第 7 步：采集程序与稳定性

- [x] 增加可见的启动/停止入口，通过互斥量避免重复实例抢占串口，使用 PID 文件等待退出；`-Stop` 在当前轮询结束后请求桥接实例退出。
- [x] 处理串口重新枚举、USB 拔插、G HUB 未启动、耳机关闭和 HID 超时；发送失败后进入下一轮重连；USB 模式可用 `-RequireAck` 逐帧等待匹配的 `ACK BAT2` 或 `ACK PERF`，超时即按发送失败处理。本机已验证 ACK 文本派生和失败清理，真实板端 ACK 仍待实机。
- [x] 每次轮询后释放 HID 句柄，设备异常不影响另一设备更新。
- [x] G HUB 未返回鼠标充电字段或设备不可用时发送 `charging=-1`，不将未知状态伪报为未充电。
- [x] 限制采集间隔为 1–3600 秒，拒绝 0 或负值间隔以避免无延迟轮询。
- [x] 启动时校验 `-PanelUrl` 必须是无用户信息、无 query/fragment 的绝对 HTTP/HTTPS 面板基址，避免错误地址、URL 凭据或 token 进入循环和 transcript。
- [x] 已在本机直接验证间隔越界、非 HTTP(S)/非绝对 URL、带 query/fragment/userinfo 的 URL 和无效 Arduino CLI 路径都会立即拒绝，不启动采集循环或临时构建。
- [x] `Test-BatteryBridge.ps1 -ValidationOnly` 将间隔上下界、协议、query、fragment、userinfo、Wi-Fi 模式误用 `-RequireAck` 和 `OfflineTestMode` 限制固化为九个可重复用例，并核对两个 `BAT2` 帧和一个 `PERF` 帧到 ACK 文本的派生结果。
- [x] 已用本机 HTTP 模拟接收端运行 `BatteryBridge.ps1 -PanelUrl ... -Once`，G HUB 不可用时收到两条 `BAT2,1,-1,0,-1` / `BAT2,0,-1,0,-1` 未知帧；验证 token 请求头会随两条帧发送，第一条返回 HTTP 500 时第二条仍正常发送，`-Once` 会以失败状态退出；支持 `-LogPath` 追加 PowerShell transcript 以保留采集日志。这些都不是实机验收。
- [x] 提供 `Test-BatteryBridge.ps1` 本机 HTTP smoke test，可重复验证两条未知 BAT2 帧和一条 PERF 帧的顺序、可选 token 请求头、transcript 不泄露 token，以及第一条 HTTP 500 时仍发送后续帧且 `-Once` 返回失败；不依赖 G HUB、ESP32 或外部网络。这不是实机验收。
- [x] 连续三轮带 token 的本机 smoke test 均通过，退出后 PID 文件和停止标记均清理；这只证明本地重复启动路径，不替代 2 小时实机稳定性测试。
- [x] 额外用不存在的串口运行 `-Once`，确认失败后 PID 文件和停止标记均清理；串口重新枚举、拔插和实际 ACK 仍待实机。
- [x] 用不存在的串口运行 `-RequireAck -Once`，确认严格 ACK 模式同样失败并清理 PID/停止文件；ACK 等待时序仍待实机。
- [ ] 用户确认运行体验后再考虑开机自启，不默默新增系统启动项。
- [ ] 连续运行至少 2 小时，观察内存、查询超时、数据年龄及音频播放是否受影响。

验收：单个设备异常时另一设备正常更新；退出程序释放 COM 端口；无重复后台和持续句柄增长。

## 验收清单

- [ ] 耳机真实电量与 NGENUITY 或耳机语音报告交叉核对（允许设备自身刷新延迟）。
- [x] 鼠标真实电量与 G HUB 同时刻读数核对。
- [x] 插入/拔出充电线，状态正确变化；未知充电状态不被误判。
- [ ] 关闭耳机、退出 G HUB、断开串口、断网，分别验证未知/过期表现。
- [ ] 非法帧、越界百分比、错误设备编号不改变界面。
- [ ] 耳机与鼠标独立过期，旧 `BAT1` 鼠标帧仍可接收。
- [ ] 中文实物布局、长文本、低电量和 `100%` 已检查。
- [ ] 每项记录测试时间、固件版本、实际结果及日志/照片位置。

## 构建与运行参考

Arduino 要求草图目录与主 `.ino` 同名，因此仓库目录如果叫 `ESP32-S3`，应先把源码复制到同名的临时草图目录再编译。以下命令创建唯一临时目录，避免覆盖已有工程；只复制构建所需文件，源码仍在仓库中维护。

```powershell
$cliCommand = Get-Command arduino-cli -ErrorAction SilentlyContinue
$cliCandidates = @(
    $cliCommand.Source,
    'E:\ArduinoIDE\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
    (Join-Path $env:TEMP 'esp32-s3-arduino-validation\tools\arduino-cli.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) }
if (-not $cliCandidates) { throw 'Install Arduino CLI or set $cli to its executable path.' }
$cli = $cliCandidates | Select-Object -First 1
if (-not (Test-Path -LiteralPath $cli)) { throw 'Install Arduino CLI or set $cli to its executable path.' }
$configArgs = @()
if ($cli -like '*esp32-s3-arduino-validation*') {
    $configArgs = @('--config-file', (Join-Path $env:TEMP 'esp32-s3-arduino-validation\config\arduino-cli.yaml'))
}
$repo = 'D:\AgentForge\proj\ESP32-S3'
$staging = Join-Path $env:TEMP ('battery-panel-' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $staging 'ESP32-S3-Touch-LCD-2.8-Battery-Display'
$null = New-Item -ItemType Directory -Path $project
Get-ChildItem -LiteralPath $repo -File | Where-Object Extension -in '.ino','.c','.cpp','.h' | Copy-Item -Destination $project
& $cli @configArgs compile --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc' $project
# 关闭占用串口的采集进程和串口监视器后，再按已确认端口上传。
& $cli @configArgs upload --port COM12 --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc' $project
```

本机通过 IDE 自带 Arduino CLI、ESP32 core `3.3.10-cn` 和 LVGL `8.3.10` 编译通过；2026-10-28 加入正反校准和监控页修正的最终程序 `1,231,628` 字节（93%），全局变量 `116,404` 字节（35%）。`--warnings all` 的警告来自 LVGL 8.3.10 上游。最终固件已烧录至 COM12 并通过 esptool 哈希验证；监控页真实数据及可见性已板端验证，实物观感、手势和 Wi-Fi 仍需现场验收。确认硬件端口后，关闭占用串口的进程再执行上传；编译完成后可按需单独运行以下采集命令：

也可直接使用仓库内脚本构建；默认会删除临时草图目录，调试构建文件时加 `-KeepStaging`：

```powershell
& "$repo\windows\Build-Firmware.ps1" -WarningsAll
```

```powershell
$repo = 'D:\AgentForge\proj\ESP32-S3'
& "$repo\windows\Read-HyperX.ps1"
& "$repo\windows\BatteryBridge.ps1" -Once
& "$repo\windows\Test-BatteryBridge.ps1"
& "$repo\windows\Test-BatteryBridge.ps1" -WithToken
& "$repo\windows\Test-BatteryBridge.ps1" -FirstResponseError -WithToken
& "$repo\windows\Test-BatteryBridge.ps1" -ValidationOnly
& "$repo\windows\Run-OfflineChecks.ps1"
```

离线 smoke test 使用 `-OfflineTestMode` 固定生成未知设备和性能帧，只允许 `-Once` 配合 loopback `-PanelUrl`，不会读取 G HUB、HID 或真实设备。普通 `BatteryBridge.ps1` 运行则始终读取当前 Windows 设备状态。

USB 连续采集在前台运行，按 `Ctrl+C` 停止：

```powershell
$repo = 'D:\AgentForge\proj\ESP32-S3'
& "$repo\windows\BatteryBridge.ps1"
```

接入 ESP32 并需要严格确认每帧已被板端接收时，可在 USB 模式使用 `-RequireAck`；电量帧等待 `ACK BAT2 <device> <level>`，性能帧等待 `ACK PERF <cpu> <memory> <gpu> <gpuTemp>`；Wi-Fi 模式不支持该参数：

```powershell
& "$repo\windows\BatteryBridge.ps1" -RequireAck
```

需要保留采集日志时，可指定已有目录中的日志文件；日志不会打印 token：

```powershell
& "$repo\windows\BatteryBridge.ps1" -LogPath "$repo\windows\battery-bridge.log"
```

需要同时重新编译固件时运行 `Run-OfflineChecks.ps1 -BuildFirmware`；该检查仍不包含烧录、屏幕、触控、Wi-Fi 或长时间实机验收。

Wi-Fi 模式需要先退出串口采集桥和串口监视器，再配置 token。以下随机值只存于当前 PowerShell 进程环境，不要打印或写入日志：

```powershell
$repo = 'D:\AgentForge\proj\ESP32-S3'
$tokenBytes = New-Object byte[] 16
$rng = [Security.Cryptography.RandomNumberGenerator]::Create()
$rng.GetBytes($tokenBytes)
$env:BATTERY_PANEL_TOKEN = [BitConverter]::ToString($tokenBytes).Replace('-', '').ToLowerInvariant()
$rng.Dispose()
[Array]::Clear($tokenBytes, 0, $tokenBytes.Length)
& "$repo\windows\Set-PanelToken.ps1" -Port COM12
```

检查网络状态后，在前台启动 Wi-Fi 采集。`battery-panel.local` 可跨 DHCP 地址变化；如果 Windows 无法解析 `.local`，先通过串口 `IP` 命令查询板卡地址：

```powershell
$repo = 'D:\AgentForge\proj\ESP32-S3'
Invoke-WebRequest 'http://battery-panel.local/health' -UseBasicParsing
& "$repo\windows\BatteryBridge.ps1" -PanelUrl 'http://battery-panel.local'
```

`/health` 响应中的 `device0` 是耳机，`device1` 是鼠标；`age0/age1` 单位为秒，`-` 表示尚未收到数据。

从另一个 PowerShell 窗口请求连续采集进程在当前轮询后退出：

```powershell
$repo = 'D:\AgentForge\proj\ESP32-S3'
& "$repo\windows\BatteryBridge.ps1" -Stop
```

在串口监视器中可输入 `HELP` 查看命令，输入 `VERSION?` 记录固件协议版本，输入 `BRIGHTNESS 60` 设置亮度、`BRIGHTNESS?` 查询、`HEALTH?` 查看网络和两台设备数据年龄，或输入 `TOKEN<TAB>CLEAR` 清除 HTTP token。未配置 HTTP token 时，`/battery` 不做认证，只应通过 USB 或可信局域网使用；不要暴露到公网。

官方资料：[产品说明](https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/) · [Arduino 开发](https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/Arduino) · [相关资料](https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/Resources-And-Documents)。

本项目是独立 Git 仓库。提交和推送应在 `D:\AgentForge\proj\ESP32-S3` 内执行，不要误操作工作区根仓库 `AgentForge`。如需首次克隆后直接符合 Arduino 目录要求，也可将克隆目标命名为 `ESP32-S3-Touch-LCD-2.8-Battery-Display`。
