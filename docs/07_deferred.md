# 07 Deferred Context：并行录制，统一回放

## 教什么、教哪些

目标：搭建线程安全的 DX11 多线程录制流程，验证命令数量和画面相同，再判断是否有性能收益。

知识点：Device / Context 线程边界、独立 Deferred Context、命令列表、完整状态绑定、FinishCommandList、ExecuteCommandList、驱动能力、任务粒度。前置：第 2、4 章。

## 执行结构

```text
工作线程 0：Context 0 → Command List 0 ┐
工作线程 1：Context 1 → Command List 1 ├→ 渲染线程 Immediate Context 逐个回放 → GPU
工作线程 2：Context 2 → Command List 2 ┘
```

这里只并行化 CPU 录制部分。普通 DX11 不向应用提供 DX12 式独立 graphics/compute/copy 队列，不能据此声称 GPU 得到了多条可独立调度的队列。

## 代码要点

读 `07_deferred.cpp`，再看 `ParticleRenderer::InitGrid / GridState`。

初始化时生成包含所有三角形的 immutable 顶点缓冲。每次 Draw(3, firstVertex) 读取不同三个顶点，因此每个网格单元真的处于不同位置。初始化资源后，工作线程只读取这些共享对象。

- 模式 0：Immediate Context 顺序发出全部 Draw。
- 模式 1：每个 worker 独占一个 Deferred Context，按区间录制相同 Draw 集合。
- 每次录制先 ClearState，随后设置 render target、viewport、拓扑、input layout、顶点缓冲、VS 和 PS；Deferred Context 不继承 Immediate 的状态。
- `FinishCommandList(FALSE)` 完成录制；所有线程汇合后才在 Immediate 上依次 `ExecuteCommandList(FALSE)`。
- FALSE 不恢复先前状态；后续每帧主动绑定需要的状态。不要假定回放后仍保留先前状态。

程序打印 `DriverCommandLists` 和 `DriverConcurrentCreates`。不支持原生命令列表时运行时仍可提供功能，但性能可能不同；不能按显卡厂商名称推断当前驱动能力。

## 动手实验

```powershell
.\build\Release\07_deferred.exe --mode 0 --draws 16384 --frames 600 --no-vsync --output out\07-immediate.csv
.\build\Release\07_deferred.exe --mode 1 --draws 16384 --workers 4 --frames 600 --no-vsync --output out\07-deferred.csv
```

把 draws 改为 64、1024、4096、16384，把 workers 改为 1、2、4。两个模式应有相同 draw_calls 和最终网格画面。update_ms 在并行模式统计派发到全部录制完成的 CPU 区间；总 CPU 提交还包含命令列表回放。

这个实验故意使用大量小 Draw 放大 API 开销。在真实游戏中应先考虑实例化、批处理和减少无用绘制，再判断剩余录制成本是否需要多线程。

## 实验结论怎么写

报告设备、驱动能力、Draw 数、worker 数、CPU 提交中位数、GPU 区间和画面验证。若并行更慢，检查工作量是否足够、Finish/Execute 成本、驱动支持以及负载分配；这不是自动等于实现错误。

自动测试除 Debug Layer 外，还检查网格可见覆盖，并比较两模式 PNG 完全相同。仅“没有崩溃”无法证明每个 Draw 都画到了正确的位置。

## 练习与验收

1. 在一个 worker 里故意移除 viewport 绑定，观察 Debug Layer 和画面，再恢复；解释状态不继承的含义。
2. 让 worker 数大于 Draw 数，推导空区间并验证空列表路径。
3. 设计实例化版本，与多线程小 Draw 比较；说明它减少了什么工作。

验收：线程不同时访问同一 Context，图像相同，未整除分区不漏 Draw，性能结论有驱动和负载条件。

来源：[Immediate/Deferred](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render)、[线程边界](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)、[驱动能力查询](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-support)。下一章：[综合实验](08_integrated.md)。
