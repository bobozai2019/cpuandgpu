# 03 动态缓冲：怎样上传才不踩到 GPU 的数据

## 教什么、教哪些

目标：理解 CPU 更新与 GPU 读取同一缓冲时的生命周期，正确使用 DISCARD / NO_OVERWRITE。

知识点：DYNAMIC、CPU_ACCESS_WRITE、Map/Unmap、逻辑内容丢弃、资源重命名、未覆盖区间、批次大小。前置：第 1、2 章。

## 问题从哪里来

CPU 已经把一个缓冲用于 Draw，GPU 可能还没有读取完。CPU 下一次更新若直接覆盖它，运行时必须保证正确性，可能产生等待。动态缓冲提供适合频繁上传的访问方式，但不是无条件零开销。

- `WRITE_DISCARD`：旧逻辑内容全部作废。运行时可以提供另一块存储，使 GPU 继续读取旧内容；具体分配和等待取决于实现及压力。
- `WRITE_NO_OVERWRITE`：应用承诺不改动 GPU 仍需读取的数据。它不会替应用查清“哪些字节已经安全”。

本章使用动态顶点缓冲。DX11.0 对 NO_OVERWRITE 的适用范围不能直接推广到常量缓冲；DX11.1 扩展能力需要额外查询。

## 两条代码路径

看 `03_upload.cpp`，核心映射逻辑在 `particles.cpp::ParticleRenderer::Cpu`。

每 1024 个粒子一个批次，模式间保持粒子数、绘制调用数一致：

```text
模式 0：每批 DISCARD → 写入 [0, batch) → Draw(batch, 0)
模式 1：首批 DISCARD → 写入 [0, batch)
        后续 NO_OVERWRITE → 写入 [first, first+batch)
        Draw(batch, first)
```

模式 1 在一帧内只前进、不回绕，整个缓冲容纳该帧所有粒子。下一帧从 DISCARD 重新开始，因此不需要把尚在途的区间直接覆盖。它是最容易推导正确性的流式上传例子。

## 动手实验

```powershell
.\build\Release\03_upload.exe --mode 0 --count 262144 --frames 600 --no-vsync --output out\03-discard.csv
.\build\Release\03_upload.exe --mode 1 --count 262144 --frames 600 --no-vsync --output out\03-stream.csv
```

比较 `upload_ms`、`draw_calls` 和 GPU 区间。两份 CSV 的每帧 draw_calls 应相同。这里 upload_ms 包含 Map、内存复制、Unmap 的 CPU 时间；不代表独立测量出的总线传输时间。

用 `--count 10003 --validate --debug` 检查最后一个不完整批次。自动测试还比较两种模式的最终 PNG，能够发现上传偏移、Draw 起点等图像错误。

## 练习与验收

1. 将批次大小从 1024 改成 256 / 4096，保持两个模式相同，比较 Map 和 Draw 的调用数量。
2. 推导为什么在同一帧回到偏移 0 时不能继续无条件 NO_OVERWRITE。参考：之前发出的 Draw 可能仍在使用那段数据。
3. 设计容量更小的环形分配器，写出回绕时的完成判断规则后再编码；不允许仅按“过了两帧”判断安全。

验收：能标出每个批次写入和读取的字节区间；两模式图像相同、Debug Layer 没有警告或错误；性能结论只针对实际测量条件。

来源：[Dynamic resources](https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources)、[Map 限制](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)。下一章：[CPU 任务并行](04_cpu_jobs.md)。
