# 本机验证记录

验证日期：2026-10-27（Asia/Shanghai）。这是功能与正确性验证，不是正式性能排名。

## 环境与构建

- Visual Studio Professional 2026，MSVC 19.51，x64。
- Windows SDK 10.0.26100.0：DX11 头文件、链接库和 D3DCompiler 47 已存在，没有下载旧版 DirectX SDK。
- 硬件设备：NVIDIA GeForce GTX 1050；软件设备：WARP。
- DX11 Debug Layer 创建成功。系统中使用 `d3d11_3SDKLayers.dll`，检测脚本兼容该文件名。
- 8 个可执行程序在 Release 和 Debug 配置均编译成功。

## 已执行的检查

### GUI 参数面板更新

- Release 与 Debug 均重新构建完成；两种配置各 16 项 CTest 全部通过。
- 新增 `tests/gui_controls.cpp`：在测试进程自己的窗口内调用真实控件消息处理，覆盖八章的数值修改、自动应用处理、立即应用、零值/越界拒绝、暂停/继续和双向模式切换。
- 控件测试同时执行真实帧循环，检查资源重新初始化次数、最终模拟步数、GPU/CPU 结果、可见渲染目标和 Debug Layer；参数切换前的样本不会混入最后一段。
- HTML 的 35 项检查通过，包括源码锚点、命令复制、计算器、离线访问和桌面/移动布局。
- 桌面自动化工具抓图连续返回 `window capture timed out`；本轮未完成原生窗口的截图视觉复核。上述原生控件验证是进程内集成测试，不是桌面点击录制。

| 检查 | 结果与范围 |
|---|---|
| Release 自动矩阵 | 28 项通过：硬件 8 章 × 2 模式，WARP 8 章模式 1，Compute 边界数量 1/255/256/257 |
| Debug CTest | 8 项通过：每章 WARP 模式 0，48 帧，带 Debug Layer |
| CPU/GPU 参考 | 更新章节检查固定初始数据、固定时间步后的粒子位置与速度；回读章节另按源帧检查历史数据 |
| 图像检查 | 所有验证运行检查实际可见像素；第 7 章检查网格覆盖；第 1/2/3/4/7 章两模式最终 PNG 完全一致 |
| DX11 诊断 | 验证运行均无 Debug Layer 警告或错误 |
| 原生窗口路径 | 第 8 章窗口模式 32 帧，数值、历史回读、渲染目标、截图与 Debug Layer 检查通过 |
| 启动脚本 | `run.ps1 -Chapter 5 --headless --frames 4 --count 1 --validate --debug` 运行通过 |
| 教材 | 8 章各有目标、知识点、实验和验收；Markdown 本地链接检查通过 |

原始证据在 [Release 矩阵报告](../out/validation/report.json)、各同目录 `.log/.csv/.json/.png`、[窗口模式报告](../out/window08.json)，Debug CTest 日志在 `build/Testing/Temporary/LastTest.log`（后续 CTest 会覆盖该文件）。这些生成目录不纳入版本控制，换机器后应自行重跑。

## 发现并修复的问题

- 初版第 7 章的多次 Draw 重叠在同一位置。仅检查 Debug Layer 与两模式相同会漏掉这一点。修正为静态顶点缓冲中的独立三角形，并加入网格可见覆盖检查。
- 启动脚本最初把透传参数当成 Configuration，已禁用隐式位置绑定并实际运行验证。
- 最终截图采用保留的独立渲染目标，避免依赖 DISCARD 交换链 Present 后的内容。
- 回读历史参考使用重置后的源帧基准，避免切模式后把全局帧号误当模拟步数。

## 验证边界

原生窗口已启动并成功渲染，但桌面自动化截图工具返回 `FrameArrived timed out`，重新选择窗口后返回 `failed to activate captured window`。因此没有宣称已通过完整中文侧栏截图和键盘交互自动化检查。程序自己的 PNG 导出仅包含 DX11 视口，已人工查看粒子与修正后的网格画面。

依赖已齐备，因此 setup 脚本的官方下载安装分支没有在本机实际执行，避免无意义地重装开发环境。它的检测分支已执行；换机器安装涉及网络、权限和可能的系统重启。

## 复现

```powershell
.\scripts\setup.ps1 -CheckOnly
.\scripts\build.ps1
.\scripts\test.ps1
.\scripts\build.ps1 -Configuration Debug
# 使用 Visual Studio 自带或 PATH 中的 ctest
ctest --test-dir build -C Debug --output-on-failure
```

本机 CMake/CTest 随 Visual Studio 安装，未加入全局 PATH。build.ps1 自动定位；手动执行 ctest 时可使用其完整路径或 Visual Studio 开发者终端。

## HTML 教学手册补充验证

`docs/handbook.html` 使用本地 Edge 浏览器离线打开，完成 35 项检查：章节/条目数量、页内锚点与相对文件链接、桌面和手机布局、搜索、源码自动展开、命令复制、打印样式，以及 5 个计算器的正常和边界输入。时间戳覆盖超过 JavaScript 安全整数范围的相邻值，确认使用整数差值再换算；零频率和 Disjoint 输入会显示无效状态。

检查期间无 JavaScript 运行错误、无网络资源请求。禁用 JavaScript 后正文、变量、函数与公式仍保留。已查看桌面首页、第五章、计算器和手机宽度截图。源码生成器再次运行得到相同 HTML 哈希，生成过程可复现。报告位于 [HTML 检查结果](../out/handbook/check-report.json)，截图在同目录；这项浏览器验证与上文原生 DX11 窗口自动化限制是不同事项。

## 2026-10-28：第 04 章绘制计时

Release、Debug 各 18 项 CTest 全部通过。新增绘制均值的源帧回填、过期和独立样本计数检查；第 04 章验证模式检查有效绘制区间不超过所属帧区间。GTX 1050 运行 180 帧、265536 粒子、10 工作线程，正确性和图像检查通过，Debug Layer 警告/错误为 0，预热后帧区间与绘制区间各 150 个有效样本。使用真实 GDI 面板函数生成并检查了 comparison-draw-timing-fixture.png（测试数据，非性能结果）。
