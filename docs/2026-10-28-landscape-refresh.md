# 2026-10-28 横屏、小字号、滑动及主动读取

基线：`master` / `c60e923`。目标板：COM12，ESP32-S3，8MB PSRAM。此次修改保留在工作区，未提交、未推送。

## 实施步骤与依赖

1. **显示和坐标一起旋转**：`Display_ST7789.cpp/.h` 使用 MADCTL `0x60` 和 320×240；`LVGL_Driver.h` 使用同样分辨率；`Touch_Driver.cpp` 将原始坐标转为 `x=rawY, y=239-rawX`。实际横屏触摸方向还需实物确认。
2. **缩小文字与布局**：`BatteryDashboard.cpp/.h` 改为两列设备、两列四行 PC 指标，正文和标题 12px，百分比 16px，进度条 142×6px；保留 `--`、充电未知、不可用、过期等不同状态。每秒更新年龄，收到采集数据更新底部反馈。
3. **精简交互**：删除 LOCK、MONITOR、BACK，使用 LVGL 手势冒泡，左滑进入 PC 页、右滑返回；右上角 30×28px 刷新图标和下滑均触发读取。
4. **端到端主动读取**：`BatteryTransport.cpp/.h` 增加请求序号、`REFRESH`、`UI?`；Windows `BatteryBridge.ps1` 持续消费 USB 请求，或每秒查询 HTTP health 序号，提前结束等待重新查询两个设备。1.5 秒防抖；20 秒没收齐两设备回复则恢复按钮并提示超时。
5. **电量校验**：`PanelProtocol.ps1` 精确匹配整行 ACK，保留 USB 分包缓冲；校验 G HUB 设备 ID、缺失/null/bool/越界百分比，保留真实 0%；`HyperXReader.cs` 仅接受当前 GET 命令响应，排除旧通知、截断和 255。
6. **回归与实机**：离线脚本测试后编译、烧录，运行板端回归，再恢复真实采集。以下百分比测试均为合成数据，不代表真实设备电量。

## 已执行验证

| 项目 | 结果与证据 |
| --- | --- |
| 编译 | `Build-Firmware.ps1 -WarningsAll -KeepStaging` 通过。最终程序 1,226,836 字节 / 默认应用分区 93%；全局变量 116,332 字节 / 35%。未升级依赖。仅见 LVGL 上游枚举与 const 警告 |
| 烧录 | esptool 上传 COM12，最终应用镜像 1,226,992 字节，`Hash of data verified`；自动复位成功 |
| 启动 | `TOUCH CST3530 READY`；`VERSION BatteryPanel/2`；HEALTH 报告 `screen=320x240`、Wi-Fi disconnected |
| 控件边界 | `UI screen=320x240 page=0 font=12 lock=removed boundsErrors=0`；此检查核对 LVGL 控件坐标，不是屏幕照片 |
| 离线 | 10 个 PowerShell 脚本解析通过；49 个中文字符，118 个字体 glyph，缺字 0；9 个参数拒绝用例、3 条 ACK 派生；token/无 token/首条 HTTP 500 继续发送及退出清理通过 |
| 电量回归 | ACK 7 不匹配 71；PERF 4 不匹配 45；缺失/null/bool/101、错误设备 ID、截断/错命令/255/0x0D 旧 HID 通知被拒绝；真实 0 和 charging=false 保留 |
| USB 分包 | 用实际生产 `Read-PanelOutput` 接收分成两段的请求，并处理同批 ACK，确认请求与 ACK 都不丢失 |
| 板端渲染值 | `Test-PanelHardware.ps1 -Port COM12` 通过：0/7/71/100/-1 显示 0%/7%/71%/100%/--；断开时即使收到 88 也显示 --；设备编号、充电状态分开更新；旧 BAT1 仍兼容 |
| 非法帧 | 101%、设备 9、充电值 2、尾随 junk 均返回 ERR FRAME，原渲染值未改变 |
| 刷新生命周期 | REFRESH 后 refreshing=1；只有一台回复时维持 1，第二台回复后为 0；重复请求被防抖；无采集桥时等待 21 秒后恢复 0 |
| 独立过期 | 同时给两台测试数据，中途仅更新鼠标；47 秒后耳机 expired / age0=47，鼠标 fresh / age1=22；渲染分别为 -- 和 7%；结束已清除合成百分比 |
| 主动读取集成 | `Test-PanelRefresh.ps1`：loopback 面板 health 序号 0→1，连续桥设置 60 秒周期，约 5.04 秒后发送下一批，共两次查询、6 帧；未等满周期，正常停止且清理 PID/stop 文件。它使用真实只读设备查询，未启用离线夹具，不等同真实 Wi-Fi 验收 |

生产 USB 分包处理函数抽取共享后再次运行主动读取集成通过，下一批间隔 5.16 秒；生产桥日志明确记录鼠标 `NOT_CONNECTED` 和耳机原始 `255`。

最终真实 USB 连续采集至少 3 轮通过：两台电量帧和 PERF 各 3 次精确 ACK，共 9 次，无 ACK 超时、发送失败或串口不可用记录。采集的设备电量均为未知；性能数据来自 Windows 实际查询。后台采集进程继续运行，未创建系统自启动项。

本机运行记录：PID `17104`，日志 `C:\Users\20878\AppData\Local\Temp\battery-panel-live-20261028-183409.log`。进程和路径仅对应本次会话，重新启动后会改变。当前后台占用 COM12；烧录、串口监视或重新运行测试前，先执行 `powershell -ExecutionPolicy Bypass -File .\windows\BatteryBridge.ps1 -Stop`。

首次刷新集成测试中，生产桥已收到请求并发送第二批数据；测试脚本漏捕获 PowerShell 信息流，误报失败。已改为捕获信息流，重新运行通过。首次字体编译因 include 路径错误失败，已使用 `--lv-include lvgl.h` 重新生成并编译通过。

## 真实电量调查

- G HUB agent 正在运行。`/devices/list` 返回 `deviceModel=pro_wireless_mouse, state=NOT_CONNECTED, id=dev00000000`；其 battery GET 返回失败。当前不能将缓存读数作为在线鼠标电量；桥输出 `BAT2,1,-1,0,-1`。
- HyperX `03F0:02CC` 接收器可回应：`GET 06 raw=255; GET 48 raw=255;`，电量和充电状态均无效，桥输出 `BAT2,0,-1,0,-1`。已保留原有 `HidD_SetOutputReport` 发送方式；没有证据将发送 API 当成此次问题原因。
- 目前已确认并修复解析和确认逻辑的缺陷，尚未获得可与 G HUB/NGENUITY/耳机语音同时对照的有效百分比，不能声称实际电量精度已验收。

## 重复运行

各桥接测试共享互斥量，需要依次运行。硬件测试会显示临时测试电量，结束后恢复未知；运行后再开启真实采集。

```powershell
.\windows\Run-OfflineChecks.ps1
.\windows\Test-PanelRefresh.ps1
# 先停止采集桥并关闭串口监视器，再运行板端测试（约 80 秒）
.\windows\Test-PanelHardware.ps1 -Port COM12
.\windows\BatteryBridge.ps1 -Port COM12 -RequireAck -IntervalSeconds 15 -LogPath "$env:TEMP\battery-panel-live.log"
```

字体重生成（修改中文文案后执行；生成器是临时工具，不修改项目依赖）：

```powershell
$source = Get-Content -Raw -Encoding UTF8 BatteryDashboard.cpp
$chars = -join (@([regex]::Matches($source, '[\u3400-\u9fff]') | ForEach-Object Value | Sort-Object -Unique))
npm exec --yes --package=lv_font_conv@1.5.3 -- lv_font_conv --font 'C:\Windows\Fonts\NotoSansSC-VF.ttf' --size 12 --bpp 4 --format lvgl --lv-include lvgl.h --symbols $chars --range 0x20-0x7E --no-kerning --lv-font-name battery_font_12 -o battery_font_12.c
.\windows\Test-FirmwareAssets.ps1
```

字体许可保留在 `fonts/NotoSansSC-LICENSE.txt`；旧 16px 字体文件未删除，当前界面不引用它。

## 待完成（按顺序）

1. **物理显示与触控**：确认横屏旋转方向，检查中文、型号省略号、100%、刷新图标可读性；分别在两列和空白处左右滑、下滑、点击刷新，核对无误触和卡顿。远程当前没有相机或触屏操作能力，不能以坐标检查代替该项。
2. **真实电量精度**：鼠标上线后记录 G HUB 与板端同一时刻读数；耳机恢复有效 GET 后与 NGENUITY 或耳机语音核对，测试充电/拔线/关机。若仍为 255，应继续查接收器/耳机连接和型号协议，不能估算百分比。
3. **真实 Wi-Fi**：配置 2.4GHz 凭据后验证 token、health 序号、触摸主动读取、断网恢复、USB 数据线断开后的更新。
4. **两小时稳定性**：记录内存、数据年龄、查询超时、串口重连和音频影响；本轮短时自动化不替代长稳验收。
5. **中文微调**：主体已中文化；产品型号和 CPU/GPU/USB 等缩写保留。后续文案或字体调整需同步重生成字库和检查实物可读性。
