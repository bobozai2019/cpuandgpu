# 04 CPU 多核：把独立数据更新拆成任务

## 教什么、教哪些

目标：把同一套游戏数据更新从串行改为多线程，并证明结果相同。

知识点：持久线程池、连续数组、不重叠写入、分区边界、汇合点、串行部分、任务调度开销与内存带宽。前置：第 3 章和 C++ 线程基础。

## 先确定可并行的边界

本例每个粒子只依赖自身位置和速度，不读取其他粒子。第 i 个工作线程处理：

```cpp
first = count * i / workers;
last  = count * (i + 1) / workers;
// 写入 [first, last)，不写其他区间
```

这种整数分区覆盖全部元素，即使数量不能整除线程数也不会漏尾部。数组在任务开始前已分配，任务过程中不 resize。各线程写不同元素，主线程在 Run 汇合后才读取和上传。

线程只处理 CPU 内存，不调用 Immediate Context。多核更新与 GPU 渲染是两种不同层次的并行。

## 阅读代码

`04_cpu_jobs.cpp::Frame` 是教学主线；`gpu.cpp::Jobs` 实现线程池。线程在初始化时建立，每次 Run 递增 generation，唤醒 worker，等待所有任务完成，最后向调用线程传播异常。条件变量 wait 带谓词，避免虚假唤醒造成错误。

- 模式 0：同一线程遍历全部粒子。
- 模式 1：固定线程池分区更新，汇合后上传。
- 两种模式调用完全相同的 `UpdateParticles`，上传和绘制方式也相同。

## 动手实验

```powershell
.\build\Release\04_cpu_jobs.exe --mode 0 --count 1048576 --frames 600 --no-vsync --output out\04-serial.csv
.\build\Release\04_cpu_jobs.exe --mode 1 --count 1048576 --workers 4 --frames 600 --no-vsync --output out\04-jobs.csv
```

再把 count 改成 1024、65536，workers 改成 1、2、4、8。首先比较 update_ms，然后才看总 CPU 提交区间。

| 现象 | 可能原因 | 下一步验证 |
|---|---|---|
| 小任务并行更慢 | 唤醒和汇合比计算更贵 | 增大数量，保留算法 |
| 更新快了但总帧时间变化小 | 上传、提交或 GPU 是主要成本 | 对比 upload_ms 和 GPU 区间 |
| 线程增加后收益停滞 | 带宽、串行部分、调度或核心数量限制 | 降低线程数并重复测量 |

本例每个粒子的计算很少，适合观察“有可并行性但未必有收益”。教学不把创建更多线程当作优化证明。

## 正确性与练习

`--validate` 使用初始种子与固定时间步，在单线程参考路径重放同样帧数，再比对所有元素。自动测试使用 10003 个粒子和 3 个工作线程，专门覆盖不能整除的情况，并比较两种模式的图像。

1. 在实验副本中添加一个所有线程共享的计数器，解释直接 `++` 为什么产生数据竞争；再改为每线程局部计数与汇总。
2. 如果改为邻居力计算，为什么不能边读邻居旧值边覆盖当前位置？参考：需要输入快照或双缓冲来定义一致的模拟步骤。
3. 根据串行占比解释加速比上限；不要把有限的 GPU 帧时间混入纯 CPU Amdahl 推导。

验收：分区不重叠、不漏元素，CPU 数值与串行参考一致；能解释小任务为何不值得并行。

DX11 线程边界来源：[Introduction to Multithreading](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)。下一章：[Compute Shader](05_compute.md)。
