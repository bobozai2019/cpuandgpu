# 测量方法与实验记录表

## 测量前

使用 Release 和硬件 GPU；先通过 `--validate --debug`，正式采样再关闭 debug。固定数量、线程数、VSync、呈现方式、帧数和后台负载。默认硬件选择专用显存最大的非软件适配器，实际名称打印在日志和 JSON 中。

每种模式单独启动进程，至少重复三次。窗口测试要记录 VSync；离屏测试每帧 Flush，与真实呈现路径不同。WARP 是功能参考，不作为 GPU 性能成绩。

## CSV 字段

| 字段 | 精确定义 |
|---|---|
| frame | 从 0 开始的实验帧号 |
| cpu_submit_ms | 从本帧清屏前到录入结束时间戳后的 CPU 时间；包含本章工作和可能的等待，不含 Present、消息处理、查询轮询、UI 刷新 |
| gpu_interval_ms | 本章 Frame 外围的 GPU 时间戳区间，可能包含 GPU 时间线上命令供应/依赖的空隙；缺失为 -1 |
| gpu_source_frame | 该 GPU 值属于哪一帧；有值时等于本行 frame，不是获取它时的帧 |
| present_ms | Present 的 CPU 调用时间；离屏为 Flush 调用时间 |
| update_ms | 本章 CPU 更新或录制区间；第 2 章特指 Draw 调用 CPU 时间；不适用的章节为 0 |
| upload_ms | 粒子动态顶点 Map/memcpy/Unmap 的 CPU 时间，不包含所有常量上传，也不是 PCIe 总线计时 |
| readback_ms | CPU 轮询/Map/复制回读数据的时间，立即模式可能包含等待 |
| draw_calls | 本帧实际发出的粒子/三角形 Draw 次数 |
| readback_age | 当前帧减最新已消费结果的源帧；未取得结果/不适用为 -1 |
| dropped_readbacks | 本帧回读环满而跳过的新采样数 |

JSON 排除 `min(30, 总帧数/4)` 个预热帧。`gpu_samples` 是预热后有效样本数，`timer_dropped_frames` 是整个运行因计时槽满跳过的帧数。无效 Disjoint 样本也不参加 GPU 统计。

窗口模式的 present_ms 还包含将独立渲染纹理复制到交换链的 CPU 提交成本；这次显示复制和交换链呈现的 GPU 工作不在 gpu_interval_ms 内。

`cpu_frame_p95_ms` 计算 `cpu_submit_ms + present_ms` 的 P95；这是实验主工作区间，不是完整呈现帧时间，也不是端到端输入延迟。当前工程没有提供端到端输入延迟测量。不要用它宣称低延迟游戏体验已被证明。

## 记录模板

| 项目 | 填写 |
|---|---|
| 日期 / CPU / GPU / 驱动 | |
| 章节 / 模式 / 命令 | |
| Release / Debug Layer / VSync / 呈现方式 | |
| 数量 / worker / Draw 数 | |
| CPU 提交中位数（3 次） | |
| GPU 区间中位数（3 次） | |
| 回读帧龄 / 跳过回读数 | |
| 有效 GPU 样本 / 计时丢样 | |
| 数值检查 / 图像 / Debug Layer | |
| 结论与下一项实验 | |

只计算有含义的加速比：例如第 4 章相同算法和数据量的 update_ms。综合章节改变多种策略时，报告整体行为，并通过独立章节定位贡献。功能测试产生的 JSON 带 Debug Layer 和很短样本，不能直接用作正式性能结论。


## GPU 帧区间与绘制区间（第 04 章）

第 04 章增加独立的 GPU 绘制时间戳：CPU 更新及 Map/memcpy/Unmap 完成后，在 Draw 前后各记录一个时间戳。它与帧区间共享同一个 disjoint 查询和源帧号，不新增 Flush、WaitIdle 或忙等待。两项分别参与 10 秒均值、有效样本计数和前后参数对比；改参数时先取回在途结果再冻结旧统计。

- **GPU 帧区间**：包围 Lesson::Frame，可能包含等待 CPU 提供后续命令的空隙；不包括开头清屏及最终复制/Present。
- **GPU 绘制区间**：只在第 04 章的单次 Draw 前后打点。排除了前面的 CPU 更新和上传调用，但仍可能受命令供给、GPU 调度、资源依赖、频率及时间戳本身开销影响，并非纯硬件忙碌时间或利用率。
- 不要把两项相减后叫作“CPU 耗时”或“GPU 空闲时间”。也不要把 CPU、GPU 和 Present 简单相加计算帧率。
- 第 04 章比较线程数时，优先看 CPU 更新；关闭垂直同步并重复 A/B/A/B，每组采满 10 秒。红绿百分比是观察值，不是统计显著性结论。
- CSV 新增 gpu_draw_ms，JSON 新增 gpu_draw_median_ms、gpu_draw_samples。其他章节尚未定义独立绘制区间，导出值为 -1、样本数为 0；不能当作零耗时。
