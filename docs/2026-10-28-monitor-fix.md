# 正反映射与监控页修正（2026-10-28）

## 问题与证据

1. 用户现场确认应正向时画面倒向。旧 AUTO 传感器符号与当前板卡显示方向的映射相反；本轮加入明确的板卡极性校正，保留 INVERT 为附加安装修正，不改变 NORMAL/REVERSED 固定模式。
2. 原手势代码左滑固定进入第二页，右滑固定返回第一页；用户从第一页右滑时仍停在电量页。此外原绑定只依赖手势向根对象冒泡，缺少页面内容区的直接处理。
3. 真实采集日志已持续发送 `PERF`（例如 CPU 80%、内存 83%、GPU 71%），不是 Windows 完全没采到系统信息。旧温度来源一直返回 -1，因此需要独立补温度读取。

## 修改范围和依赖

- `OrientationPolicy.h` / `DisplayOrientation.cpp`：用 `Orientation_Output(detected,invert)` 显式校正 AUTO 极性，真值表加入板端自测；触控继续读取实际显示方向。
- `BatteryDashboard.cpp/.h`：横向手势切换另一页，页面表面显式冒泡和绑定，处理一次后停止继续冒泡；新增 PAGE / SWIPE 诊断和八项渲染文字。
- `BatteryTransport.cpp`：接入 PAGE / SWIPE 串口诊断。原 BAT1/BAT2/PERF 帧兼容，Wi-Fi token 逻辑不变。
- `windows/AmdGpuTemperature.cs` / `BatteryBridge.ps1`：从系统 AMD 驱动读取 GPU 温度。PMLog 输出按官方结构定义解析（4 字节 size + 256 个 supported/value，GPU edge 为索引 8、单位摄氏度），验证支持位和 0–150 范围，只调用读取接口。失败维持未知，不写显卡设置、不安装依赖。采集频率沿用 PC 性能采样周期。
- `windows/Test-PanelHardware.ps1` / `Test-PanelOrientation.ps1` / `Run-OfflineChecks.ps1`：增加导航/指标回归、AMD 读取器编译检查；串口使用 UTF-8，避免诊断中文回显为问号。

AMD 官方结构与接口核对来源：`GPUOpen-LibrariesAndSDKs/display-library` 的 `include/adl_structures.h`、`include/adl_defines.h` 和 `Public-Documents/html`。RX 6600 XT 实机温度来自 `ADL2_New_QueryPMLogData_Get`，不是推算值。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| 固件编译 | `--warnings all` 通过，程序 1,231,628 字节（93%）、全局变量 116,404 字节（35%）；仍有原 LVGL 上游告警 |
| 烧录 | COM12，应用镜像 1,231,776 字节；esptool 哈希校验通过 |
| 方向策略 | `ORIENTATION TEST PASS`，含修正后的 AUTO 真值表；固定 NORMAL/REVERSED、INVERT、AUTO 命令正常，实机最终 `mode=AUTO reversed=1 invert=0` |
| 合成性能帧 | 42/68/77/65/12/345/128/86400，板端 ACK 正确；八个指标均有渲染文字，CPU 42%、内存 68%、GPU 77%、GPU TEMP 65 C 等 |
| 导航 | 从 PAGE 1 发送 SWIPE RIGHT，page=1 / monitorVisible=1；SWIPE LEFT 返回 page=0 / monitorVisible=0；boundsErrors=0。命令路径与实际手势共享切页函数，但不等同物理触摸动作已验证 |
| 完整板端回归 | 合成电量、非法帧拒绝、旧 BAT1、刷新完成/防抖/超时、两设备独立过期全部通过。结束清除测试电量；合成 PERF 已由真实采集覆盖 |
| 离线回归 | 11 个 PowerShell 脚本解析、AMD 读取器编译、字体 49 中文/118 glyph/缺字 0、电量解析、精确 ACK、请求分包、HTTP/token/错误后继续发送和清理通过 |
| 实际 GPU 温度 | AMD PMLog 首次独立查询 54°C；23:17 完整链路查询 57°C，来源日志 `AMD PMLog GPU edge adapter=0` |
| 实际监控页 | `ACK PERF 18 55 10 57`；板端 page=1 / monitorVisible=1，CPU 18%、内存 55%、GPU 10%、GPU TEMP 57 C、上传 22 KB/s、下载 7 KB/s、磁盘 45 GB、运行 333 min；数据年龄 0、边界错误 0 |
| 真实设备 | 同次查询鼠标 63% 和耳机 93%，均有精确 ACK；不是测试夹具 |

排错记录：一次离线测试在原采集桥尚运行时因共享互斥量失败，停止桥后重跑完整通过；硬件测试最初假设初始页面、中文串口编码和跨行正则导致误报，改为显式 PAGE 1、UTF-8 和正确跨行匹配后完整通过，未关闭任何产品校验。先前 Overdrive 温度接口无有效返回，最终使用 PMLog 取得真实温度。

## 当前运行和验收边界

- 最新后台 PID `12648`，COM12，严格 ACK，2 秒轮询，带姿态诊断。日志：`C:\Users\20878\AppData\Local\Temp\battery-panel-live-final.log`；只针对本次会话，重启后 PID 会改变。
- 暂留第二页供现场查看；横滑任一方向切回电量页。没有新增开机启动项，未提交、未推送。
- CPU/内存是占用百分比，显示温度是 GPU 温度。本机没有已验证的 CPU 核心温度源，不能将 GPU 或主板温度冒充 CPU 温度。
- 实物正反、文字观感和真实手指滑动需用户现场确认；串口可见性/文字检查无法替代屏幕照片。

停止采集以释放串口：

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\BatteryBridge.ps1 -Stop
```
