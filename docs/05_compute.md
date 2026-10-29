# 05 Compute Shader：让粒子数据留在 GPU

## 教什么、教哪些

目标：用 DX11 的 Compute Shader 更新粒子，让顶点着色器直接读取结果，并与 CPU 参考结果比对。

知识点：HLSL、StructuredBuffer、16 字节结构布局、SRV/UAV、numthreads、SV_DispatchThreadID、Dispatch 向上取整、边界检查、资源绑定冲突。前置：第 3、4 章。

## 一次更新的数据流

```text
初始化：CPU 初始数组 → DEFAULT StructuredBuffer
每帧：常量 dt/count → Compute UAV 写入 → 解除 UAV 绑定 → VS SRV 读取 → 绘制
```

CPU 不需要每帧拿回粒子位置。结果直接进入绘制，省掉了粒子数组逐帧上传和回读的需求。GPU 缓冲仍存在资源依赖；“不回 CPU”不等于“所有 GPU 操作无条件同时执行”。

CPU `Particle` 是四个 float，共 16 字节；HLSL 是两个 float2。StructuredBuffer 的 stride 明确设为 16。常量缓冲单独按 16 字节布局，不能把任意 C++ 结构直接猜成 HLSL 布局。

## 读代码并手算一次派发

主线：`05_compute.cpp` → `ComputeParticles::Init / Step` → `shaders/particles.hlsl::UpdateCS`。

```hlsl
[numthreads(256, 1, 1)]
void UpdateCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= particleCount) return;
    // 每个线程只更新一个粒子，不需要原子或组内同步。
}
```

CPU 使用 `Dispatch((count + 255) / 256, 1, 1)`。10003 个粒子需要 40 个工作组，实际派发 10240 个线程，其中 237 个线程立即返回。

`numthreads` 定义一个工作组的逻辑线程数量；它不代表这些线程都在同一时刻占用物理执行单元。线程组大小也不是越大越快。本章没有组间交换数据，不需要 groupshared 或 barrier。

如果扩展为共享内存归约，需要进一步学习同步：组内屏障不能充当所有工作组的全局屏障；带组同步的屏障必须由需要参与的线程正确到达。跨组阶段通常应拆分为独立 dispatch，并正确组织资源依赖。

同一个资源不能同时作为冲突的 SRV 与 UAV 绑定。本例绘制后清空 VS SRV，计算后清空 CS UAV。解除绑定是消除绑定冲突，不是 CPU 等待；DX11 的运行时/驱动负责相应资源依赖处理。

## 动手实验

```powershell
.\build\Release\05_compute.exe --mode 0 --count 262144 --frames 600 --no-vsync --output out\05-gpu.csv
.\build\Release\05_compute.exe --mode 1 --count 262144 --frames 600 --no-vsync --output out\05-cpu.csv
.\build\Release\05_compute.exe --headless --mode 0 --count 10003 --frames 64 --validate --debug
```

模式 0 为 GPU 更新、驻留绘制；模式 1 为 CPU 更新、每帧上传。比较 cpu_submit_ms、upload_ms、GPU 区间。两模式工作放在不同处理器，不能只拿 CPU 的 update_ms 对比 GPU 的 Dispatch 调用成本就宣布计算加速。

## 验证、练习和游戏用途

`--validate` 在测量结束后执行一次阻塞回读，把最终结果与 CPU 固定步长参考对照。允许小幅浮点误差；位置按周期距离比较，速度也检查。不会在正常 GPU 模式中偷偷每帧回读做验证。

练习：分别使用 1、255、256、257、10003 个粒子；修改组大小为 64/128/256 时必须同时修改 CPU 派发公式。记录结果正确性与 GPU 时间。进一步尝试添加阻力，再同步修改 CPU 参考算法。

游戏用途包括视觉粒子和其他大量独立元素更新。若 CPU 游戏逻辑每帧都必须立即使用结果，先读下一章再决定迁移是否值得。

验收：非整组数量无越界，CPU/GPU 数值相符，正常帧不回读粒子，Debug Layer 无冲突警告。

来源：[Compute Shader Overview](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader)。下一章：[回读](06_readback.md)。
