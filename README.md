# 无线设备电量显示板

基于 Waveshare ESP32-S3-Touch-LCD-2.8 的桌面电量显示项目，目标是在 240 × 320 彩屏上展示 HyperX 耳机和 Logitech 鼠标的实际电量、充电状态及数据更新时间。

## 当前状态

项目仓库：[zzzaszz/ESP32-S3](https://github.com/zzzaszz/ESP32-S3)。当前本机仓库路径为 `D:\AgentForge\proj\ESP32-S3`，本轮基于提交 `303e18e` 验证。

仓库历史记录中此前已完成并验证（不代表本轮实机验收）：

- ESP32-S3 的 ST7789 屏幕初始化及 LVGL 英文界面。
- Windows 通过 G HUB 本地接口读取 PRO Wireless 鼠标电量。
- Windows 通过匹配型号的 HID 查询读取 HyperX Cloud III S Wireless 耳机电量。
- 两台设备的数据通过 USB 串口发送给 ESP32，板端分别返回确认。
- 双设备固件编译通过、烧录校验通过；各设备独立维护数据过期状态。

本轮已将该固件刻录到当前连接的 ESP32-S3（`COM12`，VID/PID `303A:1001`），刻录后程序镜像哈希校验通过，板端返回协议和数据 ACK。串口功能、帧验证和数据过期均已实机验证；当前未能读取有效耳机/鼠标电量，屏幕文字、触控和 Wi-Fi 仍需目视/交互验收。

### 当前支持设备

当前采集桥只识别下表中的设备和显示主机，不会把任意蓝牙电池设备或相似型号自动当作受支持设备：

| 设备 | 识别方式 | 当前支持内容 | 协议编号 / 已记录结果 |
| --- | --- | --- | --- |
| HyperX Cloud III S Wireless | USB 2.4G 接收器 `03F0:02CC`，HID GET 查询 | 电量、充电状态、连接状态；异常时发送未知值 | `BAT2 device=0`；上一轮 `98%`、未充电 |
| Logitech PRO Wireless | G HUB 本地 WebSocket，匹配 `deviceModel=pro_wireless_mouse` | 电量、连接状态；G HUB 没有充电字段时使用 `charging=-1` | `BAT2 device=1`；上一轮 `71%`、未充电 |
| Waveshare ESP32-S3-Touch-LCD-2.8 | ST7789 240 × 320，目标 FQBN `esp32:esp32:esp32s3:CDCOnBoot=cdc` | 接收 `BAT1`/`BAT2`/`PERF`，USB 串口和 Wi-Fi HTTP，触控滚动、锁屏和 PC MONITOR 页面 | 本轮已烧录；串口版本/健康/亮度命令和电量/性能/非法帧 ACK 已验证；屏幕和触控待实物目视操作 |

暂不支持：其他 HyperX 型号、其他 Logitech 鼠标、直接从 ESP32 BLE 电池服务读取，以及未经适配的第三方接收器。当前方案依赖 Windows 访问专用接收器或 G HUB；电脑关机后不得继续把旧数值标成实时电量。

表中数值是历史读数，不是此刻电量。本轮 Windows 侧 HID 查询返回耳机 `255`/未知；G HUB 未运行，鼠标也返回未知。Windows 桥如成功查询，会真实发送该状态；已在屏幕板端验证电量帧 ACK，但没有使用未知状态伪造设备读数。Wi-Fi 未配置，故未完成无线全链路验收。

### 当前验收边界

| 证据 | 已覆盖 | 不能替代 |
| --- | --- | --- |
| `Run-OfflineChecks.ps1` | 7 个 PowerShell 脚本解析、字体 glyph 覆盖、9 个参数拒绝、token 请求头、两条 `BAT2` 帧和一条 `PERF` 帧、首条 HTTP 500 后继续发送、transcript 不泄露 token、PID/停止文件清理 | 触控、屏幕显示、真实 Wi-Fi 和设备电量 |
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

1. 目视检查中文、电量条、颜色、裁切和更新时间；确认板背标签对应 V1 CST328 或 V2 CST3530，并实测滑动、锁屏解锁、返回按钮。
2. 启动 G HUB、接通耳机接收器并确保鼠标在线，重跑 `BatteryBridge.ps1 -RequireAck`，将板端显示与 G HUB/NGENUITY/耳机报告的同时刻电量交叉核对。当前电脑未运行 G HUB，耳机 GET 返回 `255`，所以不能验收真实设备读数。
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
| 屏幕 | ST7789，240 × 320，纵向 |
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
| `battery_font_16.c`、`fonts/NotoSansSC-LICENSE.txt` | 页面中文/ASCII 子集字体及其 OFL-1.1 许可 |
| `BatteryTransport.cpp/.h` | USB/HTTP 数据帧校验、`BAT1`/`BAT2`/`PERF` 解析、Wi-Fi 重连/mDNS、每设备过期处理 |
| `Display_ST7789.cpp/.h` | 官方屏幕引脚与初始化 |
| `LVGL_Driver.cpp/.h`、`lv_conf.h` | 显示缓冲、时钟、字体配置 |
| `windows/BatteryBridge.ps1` | 周期采集两台设备并发送数据 |
| `windows/Build-Firmware.ps1` | 自动发现 Arduino CLI，在唯一临时草图目录编译固件 |
| `windows/Test-BatteryBridge.ps1` | 本机 HTTP smoke test，验证两条 BAT2 帧、一条 PERF 帧和可选 token 请求头；`-ValidationOnly` 验证参数拒绝 |
| `windows/Test-FirmwareAssets.ps1` | 自动核对页面中文字符与 LVGL 子集字体 glyph 覆盖 |
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

双设备数据帧：`BAT2,device,level,connected,charging`，串口以换行结束。电脑性能帧为 `PERF,cpu,memory,gpu,gpuTemp,upKbps,downKbps,diskFreeGb,uptimeSec`；GPU 或温度不可用时使用 `-1`。

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
- [ ] 对照实物确认页面、滑动、锁屏和性能页确实工作，不只检查串口。

验收：工程能在本机重新构建，耳机与鼠标分别更新，失败时显示未知；不出现固定演示电量。

### 第 2 步：建立中文字体

- [x] 整理实际会显示的中文字符，生成只含目标中文字形及 ASCII 的 LVGL 8 子集字体，避免引入完整中文字库；自动提取 `BatteryDashboard.cpp` 的 33 个中文字符并与字体 glyph 注释核对，缺字数为 0。
- [x] 准备 16px 中文正文/标题字体，保留拉丁字母、数字、百分号和设备型号字符。
- [x] 注册 Noto Sans SC 子集字体并应用到标题、状态、来源和更新时间标签；拉丁设备型号继续使用 Montserrat，并保留 OFL-1.1 许可。字体由 `lv_font_conv 1.5.3` 从 `@fontsource/noto-sans-sc 5.3.1` 生成。
- [x] 已用 Arduino CLI 编译检查固件体积：程序 1,232,536 字节，占默认应用分区 94%；全局变量 116,316 字节，占动态内存 35%。当前字体源文件为 75 KB，LVGL 完整内置 SimSun 文件约 1.09 MB。

验收：所有目标中文、英文型号和 `0% / 100% / --` 均正常显示；固件仍可编译、烧录，无缺字。

### 第 3 步：页面中文化

- [x] 标题改为“设备电量”，移除页面内开发说明和协议说明。
- [x] 状态统一为“等待数据”“已连接”“充电中”“充电状态未知”“暂不可用”“数据已过期”。
- [x] 名称用“HyperX Cloud III S”和“罗技 PRO Wireless”，长型号允许省略或另行安排，不能覆盖百分比。
- [x] 显示“最近更新”或数据年龄；只有收到真实数据后才标为实时。

验收：正常、未知、未充电、充电、采集失败、过期六类状态文字完整，且不会把“未知”显示成“0%”。

### 第 4 步：调整 240 × 320 纵向布局

建议先采用两行设备布局，不为尚未接入的设备占位：

| 区域 | 建议位置/高度 | 内容 |
| --- | --- | --- |
| 标题栏 | y=0–43，44 px | 页面名称、顶部 Wi-Fi 连接状态 |
| 耳机行 | y=52–147，96 px | 耳机图标、名称、百分比、电量条、状态 |
| 鼠标行 | y=156–251，96 px | 鼠标图标、名称、百分比、电量条、状态 |
| 底部状态区 | y=260–319，60 px | 更新时间、USB/Wi-Fi 连接状态 |

- [x] 页面左右边距按 12 px，设备行固定为 216 × 96 px，内部留白统一。
- [x] 百分比区域固定为 40 px，预留 `100%`；名称和状态在独立区域内截断，禁止与百分比重叠。
- [x] 使用现有 LVGL 音频符号显示耳机，并用 LVGL 基础对象绘制鼠标外框和滚轮，不引入额外位图资源。
- [x] 电量条固定为 196 × 8 px，更新只改数值和颜色，不重复创建控件。
- [x] 正常、低电量、充电使用不同颜色；未知和过期隐藏数值，同时使用文字区分状态。
- [x] 已按生成字体的 advance 宽度测量名称、状态和更新时间：最长鼠标名称约 133.3px，名称区域设为 134px；最终越界和裁切仍需实机确认。

验收：实机检查两设备名称、`100%`、长中文状态；无越界、重叠、裁字和更新跳动，30 分钟无明显闪烁。

### 第 5 步：交互与屏幕设置

- [x] 接入官方 CST328/CST3530 自动探测和 LVGL 指针输入；内容区支持上下滑动，加入 `MONITOR`、`LOCK`、`BACK` 触控操作，性能页显示 CPU、内存、GPU、GPU 温度、网络、磁盘和运行时间。
- [ ] 实机核对产品背面 V1/V2 标签，确认触控坐标方向、滑动惯性、锁屏解锁和按钮点击；V2 使用 CST3530，V1 使用 CST328，不能交叉使用。
- [x] 增加串口亮度设置 `BRIGHTNESS 0-100` / 查询 `BRIGHTNESS?`，设置写入 ESP32 NVS，开机恢复；输入 `HELP` 或 `?` 可查看 Wi-Fi、token、亮度、版本、健康状态和数据帧命令，`VERSION?` 返回当前协议版本；熄屏/唤醒仍待触控版本确认。
- [x] 当前未增加手动刷新，不引入额外阻塞操作；如后续加入，必须显示刷新中/完成/失败反馈，且不得在 LVGL 线程执行阻塞 USB 或网络查询。
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
- [ ] 鼠标真实电量与 G HUB 同时刻读数核对。
- [ ] 插入/拔出充电线，状态正确变化；未知充电状态不被误判。
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

本机通过 IDE 自带 Arduino CLI、ESP32 core `3.3.10-cn` 和 LVGL `8.3.10` 编译通过；当前程序 `1,232,440` 字节（94%），全局变量 `116,316` 字节（35%）。`--warnings all` 的警告来自 LVGL 8.3.10 上游。2026-10-27 固件已烧录至 COM12 并通过 esptool 哈希验证；尚未完成屏幕目视、触控或 Wi-Fi 实物验收。确认硬件端口后，关闭占用串口的进程再执行上传；编译完成后可按需单独运行以下采集命令：

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
