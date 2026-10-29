# 02 正确测量 CPU 和 GPU

## 教什么、教哪些

目标：取得可解释的 CPU 与 GPU 数据，避免把 API 调用时间误认为 GPU 工作时间。

知识点：CPU steady_clock、TIMESTAMP、TIMESTAMP_DISJOINT、Frequency、GetData 的 S_OK/S_FALSE、异步查询环、源帧编号、缺失样本。前置：第 1 章。

## 两套时钟测的是不同事情

```text
CPU：start → 调用 Draw → end              // CPU 提交耗时
GPU：timestamp A → 已提交工作 → timestamp B // GPU 时间线区间
```

`update_ms` 在本章专门记录 Draw 调用的 CPU 耗时。不能将它解释为 GPU 着色器耗时，也不能用 CPU 时钟减 GPU 时间戳。

GPU 时间公式为 `(B - A) / Frequency * 1000` 毫秒，前提是 `Disjoint == FALSE`。若计时失效或尚未获取，保留缺失状态，不填 0。

## 阅读代码

先看 `src/chapters/02_timing.cpp`，再完整读 `src/common/gpu.cpp::GpuTimer` 与 `src/common/app.cpp` 的 `consume` 回调。

一次查询槽包含 begin、end、disjoint 三个 Query：

1. 开始 Disjoint 区间，并 End 起始 TIMESTAMP。
2. 录入本章工作。
3. End 结束 TIMESTAMP，再 End Disjoint。
4. 后续帧用 DONOTFLUSH 轮询。S_FALSE 就继续做本帧的其他事。
5. 三份数据完整且有效后才换算时间，按源帧编号写回 CSV，然后复用槽。

共有 16 个槽。若都在途，本帧跳过计时并增加丢样数，不让测量强迫游戏等待。主循环正常 Present，离屏执行每帧 Flush，所以不会仅靠 DONOTFLUSH 忙等未提交的命令。

UI 中 `GPU ... (帧 N)` 是最近取得的旧帧样本；它通常与当前显示的 CPU 值不是同一帧。CSV 则把 GPU 值填回所属帧，便于离线对应。

## 动手实验

```powershell
.\build\Release\02_timing.exe --mode 0 --gpu-repeat 32 --frames 600 --no-vsync --output out\02-async.csv
.\build\Release\02_timing.exe --mode 1 --gpu-repeat 32 --frames 600 --no-vsync --output out\02-forced-wait.csv
```

模式 1 每帧强制等待，用作“为了立即知道答案而干扰被测对象”的对照。两种模式使用同一个查询实现、相同数量的 Draw。

查看 CPU API 时间、GPU 区间、Present 时间、有效样本数与丢样数。先做多次重复，再讨论差异。正式性能实验移除 `--debug`，功能验证时保留。

## 容易误读的地方

- GPU 时间戳区间可包含命令供应间隙、资源依赖、GPU 内部停顿，不是每个硬件单元的 busy 百分比。
- CPU 与 GPU 值不能直接相加当作稳态帧间隔。
- 查询环满导致丢样时，样本可能有偏差；应报告丢样率，必要时增加槽数或降低采样频率。
- JSON 中 `cpu_frame_p95_ms` 是被测 CPU 提交区间加 Present 的 P95，不覆盖消息循环、UI 刷新、输入到显示延迟。

## 练习与验收

1. 把查询槽数改成 2 和 64，各跑一次，比较丢样和 CPU 时间。
2. 解释为什么 `while(GetData(...) == S_FALSE) {}` 会改变测试对象；若一直使用 DONOTFLUSH 而没有其他提交，甚至可能无法推进。
3. 从 CSV 找出一个 GPU 有效样本，确认 `gpu_source_frame == frame`。

验收：能区分“耗时 0”和“尚无样本”，能解释两套计时区间，能报告样本数量而非只报平均值。

来源：[GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata)、[Disjoint 与 Frequency](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint)。下一章：[动态上传](03_upload.md)。
