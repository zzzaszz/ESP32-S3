# 无线设备电量显示板

基于 Waveshare ESP32-S3-Touch-LCD-2.8 的桌面电量显示项目，目标是在 240 × 320 彩屏上展示 HyperX 耳机和 Logitech 鼠标的实际电量、充电状态及数据更新时间。

## 当前状态

项目仓库：[zzzaszz/ESP32-S3](https://github.com/zzzaszz/ESP32-S3)。已重新拉取完整固件和 Windows 采集源码，本机仓库路径为 `D:\AgentForge\proj\ESP32-S3`。后续以本仓库为开发入口，原目录中的日志不代表新仓库的当前运行状态。

此前已完成并验证：

- ESP32-S3 的 ST7789 屏幕初始化及 LVGL 英文界面。
- Windows 通过 G HUB 本地接口读取 PRO Wireless 鼠标电量。
- Windows 通过匹配型号的 HID 查询读取 HyperX Cloud III S Wireless 耳机电量。
- 两台设备的数据通过 USB 串口发送给 ESP32，板端分别返回确认。
- 双设备固件编译通过、烧录校验通过；各设备独立维护数据过期状态。

| 设备 | 接口及来源 | 上一轮实测结果 | 验证依据 |
| --- | --- | --- | --- |
| HyperX Cloud III S Wireless | USB 接收器 `03F0:02CC`，HID GET 查询 | 98%，未充电 | `GET 06 reply=98`、`GET 48 reply=0`、`ACK BAT2 0 98` |
| Logitech PRO Wireless | USB 接收器 `046D:C539`，G HUB 本地 WebSocket | 71%，未充电 | `ACK BAT2 1 71` |

表中数值是上一轮测试时的读数，不是此刻电量。尚未完成屏幕实物显示、中文排版、充电/掉线切换和 Wi-Fi 全链路验收。重新拉取仓库不会自动启动采集进程。

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
| 触控 | V1 为 CST328；V2 为 CST3530；此前固件未启用触控 |
| Arduino IDE | `E:\ArduinoIDE\Arduino IDE\Arduino IDE.exe` |
| Arduino CLI | IDE 安装目录下 `resources\app\lib\backend\resources\arduino-cli.exe` |
| 已验证板卡包 | Espressif ESP32 `3.3.10-cn` |
| 图形库 | LVGL `8.3.10` |
| 构建目标 | `esp32:esp32:esp32s3:CDCOnBoot=cdc` |
| 测试串口 | `COM12`，115200；重新插拔后应重新确认端口 |

官方示例中的屏幕引脚：MOSI 45、SCLK 40、CS 42、DC 41、RST 39、背光 5。仅适用于本型号，V2 的显示兼容性仍以官方资料和实机验证为准。

## 源码职责

当前源码结构：

| 文件 | 职责 |
| --- | --- |
| `ESP32-S3-Touch-LCD-2.8-Battery-Display.ino` | 初始化、主循环 |
| `BatteryDashboard.cpp/.h` | LVGL 页面、设备行、状态更新 |
| `BatteryTransport.cpp/.h` | USB/HTTP 数据帧校验、每设备过期处理 |
| `Display_ST7789.cpp/.h` | 官方屏幕引脚与初始化 |
| `LVGL_Driver.cpp/.h`、`lv_conf.h` | 显示缓冲、时钟、字体配置 |
| `windows/BatteryBridge.ps1` | 周期采集两台设备并发送数据 |
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

双设备数据帧：`BAT2,device,level,connected,charging`，串口以换行结束。

| 字段 | 含义 |
| --- | --- |
| `device` | `0` 耳机，`1` 鼠标 |
| `level` | `0–100`，`-1` 未知 |
| `connected` | `0/1`，表示本次采集是否确认可用，不能仅凭接收器插入判定耳机在线 |
| `charging` | `0/1`，`-1` 未知；不把未知解释为未充电 |

板端返回 `ACK BAT2 <device> <level>`；保留 `BAT1,level,connected,charging` 旧鼠标帧兼容。默认每 15 秒采集一次，每台设备独立超过 45 秒无新数据时隐藏旧百分比并显示过期。

## 后续任务：分步实施

按下面顺序开发，每一步验收后再进入下一步。复选框表示待办，不能以“编译通过”代替界面或业务验收。

### 第 1 步：确认源码与建立基线

- [x] 从 GitHub 重新拉取完整项目，确认耳机、鼠标及双设备传输源码存在。
- [ ] 在新检出目录重新执行构建和采集验证，保留历史日志。
- [ ] 检查分支、未提交修改、Arduino 板卡包和 LVGL 版本。
- [ ] 重新编译并确认双设备数据可被 ESP32 接收，记录当时的真实百分比与 ACK。
- [ ] 对照实物确认页面确实展示正确数据，不只检查串口。

验收：工程能在本机重新构建，耳机与鼠标分别更新，失败时显示未知；不出现固定演示电量。

### 第 2 步：建立中文字体

- [ ] 整理实际会显示的中文字符，生成 LVGL 8 兼容的中文子集字体；优先确认字体授权并记录来源。
- [ ] 准备正文与标题所需字号，保留拉丁字母、数字、百分号和设备型号字符。
- [ ] 注册字体并统一应用到标签；只改字符串而不换字体会出现方框或缺字。
- [ ] 检查固件体积；上一版约占默认应用分区 87%，引入中文字体前须评估剩余空间，不盲目加入完整中文字库。

验收：所有目标中文、英文型号和 `0% / 100% / --` 均正常显示；固件仍可编译、烧录，无缺字。

### 第 3 步：页面中文化

- [ ] 标题改为“设备电量”，移除页面内开发说明和协议说明。
- [ ] 状态统一为“等待数据”“已连接”“充电中”“充电状态未知”“暂不可用”“数据已过期”。
- [ ] 名称用“HyperX Cloud III S”和“罗技 PRO Wireless”，长型号允许省略或另行安排，不能覆盖百分比。
- [ ] 显示“最近更新”或数据年龄；只有收到真实数据后才标为实时。

验收：正常、未知、未充电、充电、采集失败、过期六类状态文字完整，且不会把“未知”显示成“0%”。

### 第 4 步：调整 240 × 320 纵向布局

建议先采用两行设备布局，不为尚未接入的设备占位：

| 区域 | 建议位置/高度 | 内容 |
| --- | --- | --- |
| 标题栏 | y=0–43，44 px | 页面名称、整体数据状态 |
| 耳机行 | y=52–147，96 px | 耳机图标、名称、百分比、电量条、状态 |
| 鼠标行 | y=156–251，96 px | 鼠标图标、名称、百分比、电量条、状态 |
| 底部状态区 | y=260–319，60 px | 更新时间、USB/Wi-Fi 连接状态 |

- [ ] 页面左右边距先按 12 px，设备行可用宽度为 216 px，内部留白统一。
- [ ] 百分比区域固定宽度，预留 `100%`；名称和状态在各自区域内截断或换行，禁止与百分比重叠。
- [ ] 先只用矩形占位确定位置，再加入资源；图标统一使用可明确授权的位图或现有 LVGL 符号，控制资源体积。
- [ ] 电量条设置固定尺寸，更新只改数值和颜色，不重复创建控件。
- [ ] 正常、低电量、充电各用明确但克制的颜色；未知和过期隐藏数值，同时使用文字区分状态，不能只靠颜色表达。
- [ ] 按实际字体测量调整建议坐标；本表为设计起点，不是已经完成的布局。

验收：实机检查两设备名称、`100%`、长中文状态；无越界、重叠、裁字和更新跳动，30 分钟无明显闪烁。

### 第 5 步：交互与屏幕设置

- [ ] 先核对背面 V1/V2 标签，再接入对应 CST328/CST3530 官方触控驱动。
- [ ] 增加亮度设置和熄屏/唤醒；设置需持久化，开机恢复。
- [ ] 如增加手动刷新，显示刷新中、完成、失败反馈，不在 LVGL 线程执行阻塞 USB 或网络查询。
- [ ] 在硬件版本和驱动未验证前，保持显示功能可独立运行。

验收：触控坐标与显示方向一致，亮度范围有效，操作不造成卡顿或复位。

### 第 6 步：完成 Wi-Fi 联调

- [ ] 选择同一局域网的 2.4 GHz Wi-Fi；电脑可使用有线网络，但必须能访问 ESP32。
- [ ] 完成网络配置和保存，密码不写入源码、日志或 README。
- [ ] 验证 `/health` 和 POST `/battery`；确认 Wi-Fi 下两设备均正确更新。
- [ ] 加入连接失败、自动重连及 IP 变化处理；重连前旧数据应按规则过期。
- [ ] 脱离电脑 USB 数据连接，使用独立 USB 电源测试。此时仍依赖 Windows 在线采集。
- [ ] 在可信局域网内验证接收端访问控制；不要将未认证的 HTTP 接口暴露到公网。

验收：拔掉 USB 数据连接后两设备继续更新；断网超过 45 秒显示过期，联网后自动恢复。

### 第 7 步：采集程序与稳定性

- [ ] 增加可见的启动/停止入口，记录后台 PID，避免重复实例抢占串口。
- [ ] 处理串口重新枚举、USB 拔插、G HUB 未启动、耳机关闭和 HID 超时。
- [ ] 确保每次轮询后释放 HID 句柄，设备异常不影响另一设备更新。
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

本次仓库同步仅更新文档，没有重新烧录固件。Arduino 要求草图目录与主 `.ino` 同名，因此仓库目录如果叫 `ESP32-S3`，应先把源码复制到同名的临时草图目录再编译。以下命令创建唯一临时目录，避免覆盖已有工程；只复制构建所需文件，源码仍在仓库中维护。

```powershell
$cli = 'E:\ArduinoIDE\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
$repo = 'D:\AgentForge\proj\ESP32-S3'
$staging = Join-Path $env:TEMP ('battery-panel-' + [Guid]::NewGuid().ToString('N'))
$project = Join-Path $staging 'ESP32-S3-Touch-LCD-2.8-Battery-Display'
$null = New-Item -ItemType Directory -Path $project
Get-ChildItem -LiteralPath $repo -File | Where-Object Extension -in '.ino','.cpp','.h' | Copy-Item -Destination $project
& $cli compile --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc' $project
# 关闭占用串口的采集进程和串口监视器后，再按已确认端口上传。
& $cli upload --port COM12 --fqbn 'esp32:esp32:esp32s3:CDCOnBoot=cdc' $project
& "$repo\windows\Read-HyperX.ps1"
& "$repo\windows\BatteryBridge.ps1" -Once
# 连续采集，每 15 秒更新。
& "$repo\windows\BatteryBridge.ps1"
# Wi-Fi 联调通过后使用板卡的实际 IP。
& "$repo\windows\BatteryBridge.ps1" -PanelUrl 'http://BOARD_IP'
```

官方资料：[产品说明](https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/) · [Arduino 开发](https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/Arduino) · [相关资料](https://docs.waveshare.net/ESP32-S3-Touch-LCD-2.8/Resources-And-Documents)。

本项目是独立 Git 仓库。提交和推送应在 `D:\AgentForge\proj\ESP32-S3` 内执行，不要误操作工作区根仓库 `AgentForge`。如需首次克隆后直接符合 Arduino 目录要求，也可将克隆目标命名为 `ESP32-S3-Touch-LCD-2.8-Battery-Display`。
