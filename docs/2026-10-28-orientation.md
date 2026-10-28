# 自动识别横屏方向与反转

本次基于上一轮未提交的横屏/主动读取改动继续开发，保留原有修改；没有拉取覆盖、提交或推送。

## 设计与依据

- 官方参考：[V2 示例](https://files.waveshare.net/wiki/ESP32-S3-Touch-LCD-2.8/ESP32-S3-Touch-LCD-2.8-V2-Demo.zip) 内 `Arduino/examples/LVGL_Arduino/Gyro_QMI8658.*`、`I2C_Driver.*`。IMU 位于 `Wire` 总线，SDA GPIO11、SCL GPIO10；触控保持 `Wire1` GPIO1/3。
- 启动探测 0x6B / 0x6A，WHO_AM_I 要求 0x05；加速度量程 ±4g、60Hz，陀螺仪 ±512°/s、120Hz。启用寄存器自增及两类传感器，读取 0x35 起的十二字节。I²C 超时 20ms；失败保留方向，5 秒后重试。
- 每 20ms 检查；倾斜/竖起时由重力判断方向：总加速度在 0.65–1.35g，取 X/Y 较强的分量，至少 0.4g、比另一个平面分量大 25%、不少于 Z 分量的一半才接受，连续稳定 700ms 后改变方向。两轴都支持，避免 IMU 安装方向或支架倾角导致漏识别；对角线不确定区域维持方向。
- 平放时重力无法判断水平朝向，改用 Z 轴角速度积分；相对转角达到 ±150° 时翻转并扣除相应半圈角度，支持双向来回转动。低于 3°/s 的静止偏差不积分，超过 100ms 的采样中断清空累计角度，触摸/冲击/离开平放姿态也会取消累计。竖起、平放共用方向状态。
- 显示在 MADCTL 0x60 / 0xA0 间切换并整屏重绘，分辨率保持 320×240，页面和电量状态不重建。切换时清除正在进行的输入手势，等待释放。
- 触摸共用方向状态：正常 `x=rawY, y=239-rawX`；反向 `x=319-rawY, y=rawX`。四角都在有效范围内。
- 模式 AUTO / NORMAL / REVERSED 和 INVERT 修正保存到独立 NVS 命名空间。没有新增依赖；自动旋转不写 Flash。IMU 未检测到时仍能通过串口手动反转。

## 自动测试

```powershell
.\windows\Run-OfflineChecks.ps1
.\windows\Build-Firmware.ps1 -WarningsAll -KeepStaging
# 上传输出的草图目录；测试前关闭采集桥和串口监视器
.\windows\Test-PanelOrientation.ps1 -Port COM12
```

`ORIENTATION TEST` 在独立策略实例测试重力稳定时间、平放/触摸取消重力候选、摇晃/自由落体/竖屏拒绝、millis 回绕及反向四角映射；还测试陀螺仪正反半圈分别翻转一次、90° 不翻转、静止漂移不累积、触摸/姿态/采样中断清空累计。不往实际传感器注入测试数据。实机脚本切换 NORMAL / REVERSED、检查 UI 边界及 health 方向、拒绝非法命令、检查反向修正和真实传感器读取，并要求 `algorithm=accel+gyro`；结束恢复 AUTO、INVERT OFF。

## 已定位的问题与前一版验证

- 用户反馈平放无法自动翻转。板端连续六次读数 `ax≈31mg, ay≈85mg, az≈-1058mg`，`sensor=ready, valid=1, errors=0`；前一版只启用加速度并主动忽略平放，属于实现遗漏，不能靠降低重力阈值修复，因此补入陀螺仪。
- 后续实际摆放变为 `ax≈27mg, ay≈720mg, az≈-770mg`。原先假定 X 轴主导的规则会漏掉这个支架姿态，因此改为同时识别 X/Y 主导方向，并用该实测姿态及其反向作为独立回归用例。
- 前一版已实测 QMI8658 地址 0x6B、自测 PASS、NORMAL/REVERSED/INVERT/AUTO 命令通过、控件边界为 0；这些证据不证明平放旋转已通过。
- 首次编译曾因新 LCD 函数放在其调用函数声明之前失败，调整函数位置后编译通过。前一版程序 1,229,544 字节，全局变量 116,388 字节，烧录镜像哈希通过；最终含陀螺仪版本另行记录。
- 鼠标此前在 ACTIVE 状态读到 62% 并通过真实板端 ACK；G HUB 此后报告 ABSENT。采集桥已优化为默认 2 秒、鼠标即时发送、HID 枚举 30 秒/PC 指标采样 15 秒。连续日志多数间隔约 2 秒，有系统查询时单次约 3.5 秒。

## 最终含陀螺仪版本实测

2026-10-28 20:31–20:39，本机 COM12（包含后续双轴支架修正）：

| 检查 | 结果 |
| --- | --- |
| 构建 | Arduino CLI `--warnings all` 通过；最终程序 1,231,168 字节（93%），全局变量 116,404 字节（35%）。首次含 gyro 编译暴露 `degrees()` 与 Arduino 同名宏冲突，已改名 `angleDegrees()`，重新编译通过 |
| 烧录 | 最终应用镜像 1,231,312 字节，esptool `Hash of data verified`，自动复位后启动 |
| 板端策略测试 | 最终 `ORIENTATION TEST PASS`，包含正反半圈、90° 不翻、漂移与中断过滤、触控四角，以及实际 Y 主导支架姿态的正反方向和对角线不确定区域 |
| 真实传感器 | `QMI8658 READY address=0x6B`，`algorithm=accel+gyro`；静止时 `gz≈-0.4°/s, yaw=0.0, valid=1, errors=0`，符合静止死区；不是模拟 IMU 数据 |
| 固定方向与设置 | NORMAL、REVERSED、AUTO、INVERT ON/OFF、非法命令拒绝、health 方向值通过；两种显示方向均 boundsErrors=0 |
| 重启恢复 | 写入 REVERSED、INVERT ON，使用 esptool run 复位；重新读到 `mode=REVERSED reversed=1 invert=1`。随后恢复 AUTO、INVERT OFF |
| 离线回归 | 11 个 PowerShell 脚本解析、字体覆盖、电量/ACK/分包、token 正常/异常请求、PID 清理通过 |
| 主动读取回归 | 优化桥接后重复集成测试通过；周期 60 秒时 health 序号变化约 1.14 秒后发下一批，共 6 帧，结束正常清理 |
| 设备恢复 | 20:33 曾读到鼠标 61%；20:37–20:39 最终后台连续读到鼠标 68%/未充电、耳机 GET 06 raw=96 / GET 48 raw=0（96%/未充电），持续收到 `ACK BAT2 1 68` 和 `ACK BAT2 0 96`，鼠标发送间隔约 2.1 秒；均为真实查询而非测试夹具 |

最终后台进程 PID `18712`，USB 严格 ACK、间隔 2 秒、开启 `-TraceOrientation`；本机会话日志 `C:\Users\20878\AppData\Local\Temp\battery-panel-live-gyro-trace.log`，包含真实采集和姿态诊断。没有增加系统启动项。停止命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\windows\BatteryBridge.ps1 -Stop
```

以上证明代码策略、实际 IMU 读取、配置恢复和传输路径通过；没有远程机械转动板子或取得屏幕照片，实际平放转动的图像和触控方向尚待用户现场验收。

## 实物验收

1. 长边水平持握，静止约 1 秒，再在屏幕平面转动 180°，确认图像始终朝上。
2. 在两个方向分别左右滑动切页、下滑读取并点击刷新图标，确认坐标与图像一致。
3. 平放后在桌面转过 180° 再反向转回，确认各翻转一次；静止和轻微晃动不频繁切换。触摸期间不累计转角，放手后再旋转。极慢旋转（低于角速度死区）、高速超量程或采样长时间中断会丢失相对转角，需要重新转动或竖起定位。
4. 如实际传感器轴符号与安装方向相反，使用 `ORIENTATION INVERT ON`，重启确认修正和模式恢复。

远程当前无法搬动板子或观察物理像素，不能把策略自测或串口方向字段当作这些实物操作已通过。

六轴传感器没有磁力计。开机时平放的绝对朝向无法感知，当前以启动/最近竖起方向作为相对基准；本方案不宣称水平绝对航向或长时间惯性导航能力。
