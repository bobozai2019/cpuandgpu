# DX11 CPU / GPU 并行教学工程

新增独立 **[CPU 并发实验室](cpu-parallel-lab/README.md)**：CPU 光线追踪、逐块成图、真实线程时间线、单线程 / 静态分块 / 动态取任务对照，以及 1/2/4/8 线程重复基准。想更直观地观察 CPU 多核收益，可先双击 [Start-Lab.cmd](cpu-parallel-lab/Start-Lab.cmd)，再点“一键对照”。

这是面向游戏开发的 **8 章可运行实验**。学习目标是理解 CPU 与 GPU 的执行关系，正确测量瓶颈，并通过对照实验验证优化。所有章节使用 C++17、Direct3D 11 和 HLSL Shader Model 5.0；每章有独立可执行文件、中文教学面板、源码和教材。

**推荐先打开 [HTML 教学手册](docs/handbook.html)**：双击即可离线阅读，包含 8 章导读、36 条变量/状态说明、42 条函数/调用说明、12 组公式和 5 个交互计算器。支持搜索、页内源码跳转、复制实验命令和打印。变量说明包含类型、单位、生命周期及未使用字段；函数说明包含输入输出、CPU/GPU 执行位置和是否等待。源码修改后运行 `python scripts/generate_handbook.py` 可更新 HTML 中的源码快照；阅读手册本身不需要 Python。

## 快速开始

**看不懂工程时，从手册的“第一次读这些工程”开始。** 八章均有场景解释、数据归属、真实 `Frame` 代码与逐段带读、公共函数展开、具体数字手算、实验指标和常见误区；每章末尾提供两种模式的独立运行命令。详细带读内容维护在 `scripts/handbook_walkthroughs.py`，随生成器一同生成到离线 HTML。

**直接双击根目录的 `Start-Learning.cmd`**：按 1–8 启动对应章节，H 打开中文 HTML 手册，P 打开 Visual Studio 工程。当前机器已生成 Debug 和 Release 程序；Visual Studio 默认启动项目为第 1 章，可直接按 F5。教材、HLSL 也已加入解决方案的 `course_materials` 项目。

需要重新生成全部工程和程序时，双击 `Build-All.cmd`。

在项目根目录打开 PowerShell：

```powershell
.\scripts\setup.ps1 -CheckOnly
.\scripts\build.ps1
.\scripts\run.ps1 -Chapter 1
```

如果系统执行策略禁止本地脚本，可用 `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1` 仅为这次进程运行脚本，不必修改全局执行策略。

窗口左侧是本章目标、知识点、参数和实时指标，中间是 DX11 渲染预览，右侧是设置与性能对比表。窗口可拉宽或最大化；缩窄时只缩放预览，实际渲染分辨率仍保持 800×640，避免改变实验负载。

**运行时直接使用 GUI 调参，无需命令行。** 左侧下拉框选择模式，数字框修改参数，停顿 0.6 秒后自动应用，也可点“立即应用”。暂停、继续和垂直同步均有按钮。越界输入会显示范围并保留原有效值。

| 章节 | GUI 中可修改的数值 |
|---|---|
| 01 | 粒子数、CPU 负载毫秒、GPU 重复次数 |
| 02 | 粒子数、GPU 重复次数 |
| 03、05、06 | 粒子数 |
| 04、08 | 粒子数、工作线程数 |
| 07 | 工作线程数、Draw 次数 |

应用参数或切换模式会在帧边界等待 GPU 后重建资源，重置模拟与计时样本；暂停时也可修改，继续后显示新画面。若启用了文件输出，只导出最后一组参数的数据，避免混合不同负载的统计。原命令行接口继续用于自动化验证和批量采样。

**右侧自动保留上一组设置和数据，便于直接比较。** 修改数值、模式或垂直同步时，先收齐旧配置已提交的 GPU 时间戳，再冻结旧配置最近 10 秒均值。表格逐项显示上次、当前及变化百分比，包含 CPU/GPU、更新、呈现、上传、回读、Draw、回读帧龄和丢样；绿色表示减少，红色表示增加，Draw 变化不作优劣判断。基准为零时显示绝对差，无有效样本时显示“—”。同时列出采样时长及 CPU/GPU 有效样本数，未满 10 秒会提示。

建议先让一组配置运行至少 10 秒，再改参数，并让新配置也运行满 10 秒后比较。相同设置重复应用不会覆盖对照；暂停或快速调整产生的无样本配置也不会覆盖上次有效记录。记录保存在本次运行的内存中，关闭程序后不保留。垂直同步可能掩盖帧率差异，比较时应同时观察 CPU 更新、GPU 区间等分项耗时。

- **M**：切换对照模式并重置模拟状态；切换时会显式等待和重建资源，不计入日常实验方法。
- **空格**：暂停 / 继续。
- **V**：开关垂直同步。交互模式默认开启。
- **Esc**：退出。

也可以打开 `build/DX11ParallelClassroom.slnx`（以实际 CMake 生成的 `.sln` / `.slnx` 为准），在 Visual Studio 中设置任意一章为启动项目；或者直接用 Visual Studio 打开此文件夹的 CMake 工程。

## 每章教什么

| 章 | 教学目标 | 具体内容 | 教材 / 源码 |
|---|---|---|---|
| 01 | 建立 CPU/GPU 异步执行模型 | Immediate Context、驱动缓冲、Present、跨帧重叠、每帧等待的代价 | [教材](docs/01_pipeline.md) · [源码](src/chapters/01_pipeline.cpp) |
| 02 | 正确测量两端成本 | CPU 计时、GPU 时间戳、Disjoint、查询环、源帧归属 | [教材](docs/02_timing.md) · [源码](src/chapters/02_timing.cpp) |
| 03 | 正确组织动态上传 | DYNAMIC、DISCARD、NO_OVERWRITE、分块范围与资源重命名 | [教材](docs/03_upload.md) · [源码](src/chapters/03_upload.cpp) |
| 04 | 把游戏数据更新拆成 CPU 任务 | 固定线程池、不重叠写入、汇合、粒度与并行开销 | [教材](docs/04_cpu_jobs.md) · [源码](src/chapters/04_cpu_jobs.cpp) |
| 05 | 将批量更新放到 GPU | StructuredBuffer、SRV/UAV、线程组、Dispatch、边界、GPU 驻留 | [教材](docs/05_compute.md) · [源码](src/chapters/05_compute.cpp) |
| 06 | 避免不必要的回读阻塞 | staging、CopyResource、Map、EVENT、完成查询、回读环与背压 | [教材](docs/06_readback.md) · [源码](src/chapters/06_readback.cpp) |
| 07 | 评估多线程命令录制 | Deferred Context、Command List、状态、驱动能力、串行回放 | [教材](docs/07_deferred.md) · [源码](src/chapters/07_deferred.cpp) |
| 08 | 组合完整的一帧并解释结果 | CPU 游戏对象 + GPU 视觉粒子 + 延迟回读，基线比较 | [教材](docs/08_integrated.md) · [源码](src/chapters/08_integrated.cpp) |

建议顺序是 01 → 02 → 03 → 04 → 05 → 06 → 07 → 08。先具备 C++ 数组、函数、类和基本线程知识；不要求先写过完整游戏引擎。对 Win32 / DX11 初始化不熟悉时，先读 [工程导览](docs/00_orientation.md)。

## 依赖和 SDK

- Windows 10/11 x64，Visual Studio 2022/2026 的 MSVC C++ 工具、CMake 3.24+。
- **Windows SDK 中的 Direct3D 11 SDK 组件**：`d3d11.h`、`d3dcompiler.h`、`d3d11.lib`、`dxgi.lib`、`d3dcompiler.lib`。不是 DX12 工程，也不依赖 D3DX。
- GPU 支持 Feature Level 11_0；`--warp` 可以使用系统的软件实现做功能验证，不能代表真实 GPU 的性能。
- 带 `--debug` 的验证需要 Windows 的 Graphics Tools 可选组件。检测脚本会报告其状态。

本机已检测到 Visual Studio 2026、Windows SDK 10.0.26100.0，以及 DX11 编译链接组件，因此没有额外下载旧版 SDK。微软已经把现代 DirectX 开发组件放在 Windows SDK 中，详见 [Where is the DirectX SDK?](https://learn.microsoft.com/en-us/windows/win32/directx-sdk--august-2009-)。

在新机器缺少依赖时运行 `scripts/setup.ps1`：自动检测并从微软官方入口安装所需的 C++ Build Tools / SDK；CMake 缺失时通过 winget 安装 Kitware 官方包。安装器会校验微软数字签名。Graphics Tools 如缺失，可在管理员 PowerShell 中运行 `scripts/setup.ps1 -InstallGraphicsTools`。系统安装可能触发 UAC 或要求重启，脚本会报告而不是掩盖失败。

## 跑一次可以复现的实验

```powershell
# 第 5 章：GPU 更新，验证非整工作组数量的边界，并导出结果
.\build\Release\05_compute.exe --headless --frames 180 --count 10003 --mode 0 --validate --debug --output out\compute.csv --capture out\compute.png

# CPU 对照：相同模拟帧数和相同粒子数量
.\build\Release\05_compute.exe --headless --frames 180 --count 10003 --mode 1 --validate --debug --output out\cpu.csv

# 所有章节、两种模式、软件设备以及边界数量
.\scripts\test.ps1
```

性能测量应使用 **Release、硬件 GPU、固定参数**，正式采样时移除 `--debug`；功能验证时保留 Debug Layer。独立进程分别运行两种模式，避免把按 M 后重置的混合样本当成单一模式结果。测试只检查正确性，不要求“优化版本一定更快”。

`--headless` 使用离屏纹理，省略交换链显示；每帧 `Flush` 推动提交但不等待。它与真实呈现路径不完全相同，适合功能检查和局部比较；游戏的帧节奏还需要在窗口模式下测量。

| 参数 | 含义 / 默认值 |
|---|---|
| `--mode 0/1` | 本章对照模式，默认 0；含义见各章 |
| `--frames N` | 执行 N 帧后退出；窗口默认持续，离屏默认 120 |
| `--count N` | 粒子数，默认 65536，范围 1..1048576 |
| `--workers N` | CPU 工作线程数，默认 4，范围 1..32 |
| `--draws N` | 第 7 章绘制调用数，默认 4096，范围 1..65536 |
| `--cpu-ms N` | 第 1 章模拟 CPU 忙碌时间，默认 0，范围 0..100 |
| `--gpu-repeat N` | 第 1/2 章重复绘制次数，默认 1，范围 1..128 |
| `--no-vsync` | 窗口模式不等待垂直同步；不代表无显示延迟 |
| `--headless / --warp` | 离屏执行 / 软件设备 |
| `--validate / --debug` | 结果检查 / DX11 Debug Layer（缺失时明确失败） |
| `--output PATH.csv` | 每帧 CSV + 同名 JSON 汇总 |
| `--capture PATH.png` | 保存最终 DX11 视口图片，发生在测量结束后 |

模拟固定为每次更新 `1/60 s`，确保不同机器和模式的结果可比；运行得快时动画也会走得快。这是实验的时间推进方式，不是推荐的完整游戏循环。

## 文件结构与测量解释

```text
src/chapters/       每章的核心流程（优先阅读）
src/common/app.cpp 窗口、主循环、参数、CSV、验证流程
src/common/gpu.cpp DX11 初始化、时间戳、线程池、截图
src/common/particles.cpp 上传、绘制、Compute 和回读环
shaders/particles.hlsl   GPU 实际执行的 HLSL
docs/              中文教材、作业、测量方法与验证记录
scripts/           依赖检测、构建、运行和测试
build/             生成的 VS 工程与可执行文件
out/               本机实验产物和验证证据
```

CPU API 计时不代表 GPU 执行耗时。GPU 时间戳区间也可能包含命令供应不足造成的气泡，不能简单等同于 GPU 运算单元忙碌时间。CSV 缺失 GPU 样本用 `-1` 表示，不当作 0；查询环满时跳过采样而不等待。JSON 汇总会排除最多 30 帧的预热段，并报告样本数及丢样数。完整字段说明见 [测量与实验记录](docs/measurement.md)。

本工程重点是教学用的执行模型、正确性与可测量对照。不同 GPU、驱动、数据规模可能得出不同速度结果；这是需要解释的实验结果。

已经完成的本机检查、原始报告位置与验证边界见 [验证记录](docs/verification.md)。

### 最近 10 秒均值

窗口所有运行指标使用最近 10 秒运行时间内的逐帧算术均值，约每 150ms 刷新。不足 10 秒显示实际采集时长；暂停冻结窗口，调参重置统计。CPU、GPU、更新、呈现、上传、回读、Draw、回读帧龄和回读丢样都纳入显示；GPU 按源帧回填，负值缺失样本不参与均值，零耗时仍是有效值。CPU/GPU 显示有效样本数，回读丢样单位为次/帧。CSV 仍保存逐帧原始数据。


## GPU 帧区间与绘制区间（第 04 章）

第 04 章增加独立的 GPU 绘制时间戳：CPU 更新及 Map/memcpy/Unmap 完成后，在 Draw 前后各记录一个时间戳。它与帧区间共享同一个 disjoint 查询和源帧号，不新增 Flush、WaitIdle 或忙等待。两项分别参与 10 秒均值、有效样本计数和前后参数对比；改参数时先取回在途结果再冻结旧统计。

- **GPU 帧区间**：包围 Lesson::Frame，可能包含等待 CPU 提供后续命令的空隙；不包括开头清屏及最终复制/Present。
- **GPU 绘制区间**：只在第 04 章的单次 Draw 前后打点。排除了前面的 CPU 更新和上传调用，但仍可能受命令供给、GPU 调度、资源依赖、频率及时间戳本身开销影响，并非纯硬件忙碌时间或利用率。
- 不要把两项相减后叫作“CPU 耗时”或“GPU 空闲时间”。也不要把 CPU、GPU 和 Present 简单相加计算帧率。
- 第 04 章比较线程数时，优先看 CPU 更新；关闭垂直同步并重复 A/B/A/B，每组采满 10 秒。红绿百分比是观察值，不是统计显著性结论。
- CSV 新增 gpu_draw_ms，JSON 新增 gpu_draw_median_ms、gpu_draw_samples。其他章节尚未定义独立绘制区间，导出值为 -1、样本数为 0；不能当作零耗时。
