# 01 CPU 与 GPU 如何同时工作

## 教什么、教哪些

目标：能够画出一帧的 CPU / GPU 时间线，区分“命令 API 返回”“提交到 GPU”“GPU 完成”“呈现到屏幕”。

知识点：Immediate Context、运行时和驱动缓冲、Present、跨帧流水线、EVENT 查询、显式等待、吞吐量与延迟。前置：[工程导览](00_orientation.md)。

## 先建立模型

CPU 在执行游戏逻辑、组织资源和调用 Draw；GPU 在处理先前送达的绘制。两者不是每次 Draw 都握手等待。

```text
时间向右，长度仅作示意：
CPU：准备 N、提交 N | 准备 N+1、提交 N+1 | 准备 N+2 ...
GPU：     执行先前帧 | 执行 N             | 执行 N+1 ...
```

在理想的两阶段模型中，CPU 6 ms、GPU 10 ms：串行约 16 ms 出一帧，充分重叠后的稳定吞吐量可接近 10 ms 出一帧。它不是输入到显示的完整延迟模型；排队、垂直同步、显示扫描、共享内存带宽都可能改变结果。

Immediate 中的“立即”指提交接口的角色，不表示 GPU 已完成。`Flush` 推动缓冲命令送出，也不等同于等待。`Device::WaitIdle` 则创建 EVENT、End、Flush，并有超时地轮询完成；这是有意构造的阻塞点。

## 代码与两种模式

读 `src/chapters/01_pipeline.cpp::Frame`，再看 `src/common/gpu.cpp::Present / WaitIdle`。

- 模式 0：CPU 模拟忙碌 → 发出绘制 → 返回主循环，由 Present 推动执行。
- 模式 1：上述绘制后额外等待 GPU 完成，才进入下一帧。

两者画面相同。资源在初始化时创建，粒子保持静止，避免把物理更新差异混入实验。

## 动手实验

```powershell
.\build\Release\01_pipeline.exe --mode 0 --cpu-ms 4 --gpu-repeat 32 --no-vsync --frames 600 --output out\01-async.csv
.\build\Release\01_pipeline.exe --mode 1 --cpu-ms 4 --gpu-repeat 32 --no-vsync --frames 600 --output out\01-wait.csv
```

先用窗口看现象，再看 CSV。保持粒子数、窗口大小和其他程序负载一致。`--cpu-ms` 是真实占用 CPU 的模拟计算，不是 Sleep；`--gpu-repeat` 重复同样绘制，既增加 GPU 工作也会增加少量 CPU 调用。

| 改变 | 观察 | 如何解释 |
|---|---|---|
| CPU 时间 0 → 4 → 8 ms | CPU 提交和整段调用时间 | CPU 端限制是否更明显 |
| GPU 重复 1 → 32 → 64 | GPU 时间戳区间 | 图形工作增长是否成为限制 |
| 模式 0 → 1 | CPU 等待增加，流水线机会减少 | 等待不是免费同步 |
| 开启 VSync | Present 时间可能增加 | 不应把所有 Present 时间都当作着色器成本 |

负载很轻时两种模式可能接近；不能保证本机一定得到某个倍数。GPU 时间戳围绕 Frame 区间，也可能包含 CPU 供应不足的间隙，并非“纯 GPU 忙碌计数”。

## 练习与验收

1. 画出 `CPU → Draw → WaitIdle → 下一帧` 的依赖边，标出与模式 0 不同的边。
2. 在另存的实验分支里把 WaitIdle 换成 Flush，预测并测量结果。参考：Flush 可以提前提交，但不会提供完成保证。
3. 回答“为什么只有一个 CPU 线程也能让 CPU/GPU 并行？”参考：它们是独立处理器，命令执行异步。

验收：能说明函数返回、完成和显示之间的区别；能用实际测量解释一次等待的影响。

来源：[Immediate and Deferred Rendering](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render)、[Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush)。下一章：[正确计时](02_timing.md)。
