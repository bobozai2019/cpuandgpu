# 08 综合实验：组织游戏的一帧

## 教什么、教哪些

目标：把不同处理器上的工作组织成有明确依赖的一帧，通过证据判断瓶颈是否改变。

知识点：CPU 游戏对象更新、GPU 视觉粒子更新、数据驻留、上传、延迟回读、工作安排、吞吐量与数据延迟、实验变量控制。前置：第 1–7 章。

## 场景与依赖

场景含两组粒子，作为教学简化：一组代表 CPU 必须掌握的游戏对象，一组代表 GPU 负责的视觉粒子。两组初始分布相同且采用同样运动规则，便于验证。

CPU 对象必须先完成更新再上传绘制；GPU 视觉粒子必须先 Compute 再用 SRV 绘制；统计回读只需要一个已完成的历史结果。

```text
CPU 更新 ─→ 动态上传 ─→ CPU 对象绘制
GPU Compute ─────────→ 视觉粒子绘制
          └─────────→ staging 复制 ─→ 完成查询 ─→ CPU 消费历史结果
```

CPU 更新与 GPU 视觉粒子之间没有相互数据依赖，因此可以安排工作以增加重叠机会。DX11 驱动仍有提交缓冲；“代码先写 Dispatch”并不保证硬件立刻开始执行，实际时间线需要测量。

## 两种模式与代码

读 `08_integrated.cpp`。模式 0 先录入 GPU 计算/绘制，再用线程池更新 CPU 对象，轮询旧回读、安排新回读，最后上传并绘制 CPU 对象。

模式 1 单线程更新 CPU 对象，再录入 GPU 计算/绘制，立即回读并等待，然后上传、绘制 CPU 对象。

两种模式每帧都更新相同数量的两组粒子，执行两次粒子绘制，并安排统计复制。异步环满时可跳采样，因此消费统计的频率可能不同。不得把这个综合比较误写成“单一技术的净加速比”；需要回到第 4 和第 6 章分别隔离变量。

没有强行把 Deferred Context 塞进此场景：这里只有少量 Draw，录制并行的调度成本很可能不值得。第 7 章才是评估大量小 Draw 的独立实验。

## 动手实验

```powershell
.\build\Release\08_integrated.exe --mode 0 --count 262144 --workers 4 --frames 600 --no-vsync --output out\08-organized.csv
.\build\Release\08_integrated.exe --mode 1 --count 262144 --workers 4 --frames 600 --no-vsync --output out\08-serial.csv
```

先进行 `--validate --debug` 的正确性检查，再做移除 debug 的性能采样。至少重复三次。记录 CPU 提交、update、upload、readback、GPU 区间、统计数据帧龄及丢样。

## 毕业作业

交付一份简短实验记录，包含：

1. 目标：要缩短哪段时间，是否允许统计结果延后？
2. 初始假设：瓶颈在 CPU 更新、上传、命令提交、回读等待还是 GPU？
3. 依赖图：哪些边必须等待，哪些任务能独立推进？
4. 对照条件：硬件、驱动、构建类型、数量、线程、VSync、窗口/离屏。
5. 数值：三次采样的中位数与波动，缺失样本率，结果帧龄。
6. 正确性证据：CPU 参考检查、图像和 Debug Layer。
7. 结论：在什么条件下改善，代价是什么，下一项实验是什么。

扩展方向：可先做 GPU 剔除 + 间接绘制，或 GPU 归约只回读小统计值。它们需要额外的容量、计数与资源依赖设计，不属于本工程已实现内容。

验收：可以用依赖和时间数据解释一次实际改动；能接受“这个场景不值得多线程/不值得迁移到 GPU”的证据结论。

回顾来源：[DX11 多线程](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)、[回读与并行](https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/copying-and-accessing-resource-data)。实验记录：[measurement.md](measurement.md)。
