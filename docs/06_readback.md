# 06 回读：如何拿到 GPU 的答案而不立刻等它

## 教什么、教哪些

目标：正确区分“已请求复制”和“CPU 可以读取”，建立有容量上限的异步回读流程。

知识点：STAGING、CPU_ACCESS_READ、CopyResource、Map READ、EVENT、DONOTFLUSH、DO_NOT_WAIT、环形槽位、数据帧龄、背压。前置：第 5 章。

## 两条路径

模式 1 是同步基线：

```text
Compute 更新 → CopyResource 到 staging → 立即 Map READ → CPU 使用
```

Map 若发现 GPU 尚未完成复制，就可能让 CPU 等待。两种模式均在初始化时创建 staging，不把“每帧创建资源”混入阻塞对照。

模式 0 使用 4 个槽，每槽拥有独立 staging、EVENT Query、源帧编号与 pending 状态：

```text
每帧先检查旧槽：GetData == S_OK → Map DO_NOT_WAIT → 消费 → 释放槽
再请求新样本：找空槽 → CopyResource → End(EVENT) → 标记 pending
```

没有空槽就跳过这次采样，增加 dropped_readbacks；不覆盖旧槽、不偷偷等待。这个策略适合可跳帧的统计数据，不适合必须一项不漏的任务队列。后者需要明确阻塞、限流或扩容政策。

## 阅读实现

`06_readback.cpp::Frame` 展示两条路径；`particles.cpp::ReadbackRing` 是完整环实现。

- Copy 后发出的 EVENT 用于确认之前的工作完成。
- GetData 的 S_FALSE 是正常“未就绪”，不是错误。
- 即便查询完成，Map 仍用 DO_NOT_WAIT；遇到 WAS_STILL_DRAWING 就稍后再试。
- 消费多个槽时按源帧号保留最新结果，不能假定槽数组顺序等于完成顺序。
- 每帧正常提交命令，不能在没有提交机会的情况下无限 DONOTFLUSH 轮询。

“延迟两帧后读取”只是调度策略。GPU 快慢和排队深度会变化，正确性必须依据完成状态。

## 动手实验

```powershell
.\build\Release\06_readback.exe --mode 0 --count 262144 --frames 600 --no-vsync --output out\06-ring.csv
.\build\Release\06_readback.exe --mode 1 --count 262144 --frames 600 --no-vsync --output out\06-blocking.csv
```

观察 readback_ms、readback_age、dropped_readbacks 与 CPU 提交时间。延后模式的结果可能老几帧；这个延迟是改变数据使用时机换来的。同步模式每帧读取，延后模式压力大时跳采样，两者的消费频率不一定相同，报告性能时必须同时报告采样数。

本例为放大传输问题，复制完整粒子缓冲。真实游戏若只需要一个统计值，应在 GPU 上做归约后仅回读小结果。本章没有实现归约，属于后续练习。

## 验证和练习

验证会先检查最终 GPU 数值，再按回读样本的**源帧**重放 CPU 参考，检查取回的历史数据；不会拿旧结果和当前帧比较。等待发生在测量结束之后。

1. 把槽数从 4 改为 2 / 8，观察帧龄、丢采样、内存开销。
2. 绘制自己的槽状态机：空闲 → 复制在途 → 可读取 → 空闲；标注所有允许复用的条件。
3. 设计“不可丢结果”版本，说明如何向上游施加压力，并预测它可能增加的延迟。

验收：所有历史样本按源帧验证，满环不覆盖、不越界，正常异步模式不忙等。

来源：[回读与流水线停顿](https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/copying-and-accessing-resource-data)、[GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata)、[Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)。下一章：[命令录制](07_deferred.md)。
