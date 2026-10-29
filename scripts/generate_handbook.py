"""Build the standalone Chinese handbook using only Python's standard library.

Run from any directory: python scripts/generate_handbook.py
The generated HTML has no CDN, fetch, framework or server dependency.
"""
from pathlib import Path
from html import escape as esc
import json
import re
from handbook_walkthroughs import render_foundations, render_walkthrough

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'docs' / 'handbook.html'


def code(text):
    return '<code>' + esc(text) + '</code>'


def table(headers, rows):
    return '<div class="table-scroll"><table><thead><tr>' + ''.join('<th scope="col">'+h+'</th>' for h in headers) + '</tr></thead><tbody>' + ''.join('<tr>'+''.join('<td>'+str(c)+'</td>' for c in row)+'</tr>' for row in rows) + '</tbody></table></div>'


def block(text, language='C++'):
    return '<div class="code-block"><div class="code-label">'+esc(language)+'</div><pre><code>'+esc(text)+'</code></pre><button class="copy" type="button">复制代码</button></div>'


def source_id(path):
    return 'source-' + re.sub(r'[^a-zA-Z0-9]+', '-', path)


SOURCES = ['src/common/classroom.h','src/common/rolling_metrics.h','src/common/app.cpp','src/common/gpu.cpp','src/common/particles.cpp','shaders/particles.hlsl'] + [str(p.relative_to(ROOT)).replace('\\','/') for p in sorted((ROOT/'src/chapters').glob('*.cpp'))]
SOURCE_TEXT = {p:(ROOT/p).read_text(encoding='utf-8') for p in SOURCES}


def srclink(path, needle='', label='查看实现'):
    lines = SOURCE_TEXT[path].splitlines()
    line = next((i+1 for i,s in enumerate(lines) if needle and needle in s), 1)
    return f'<a class="source-link" href="#{source_id(path)}-L{line}">{esc(label)} <span aria-hidden="true">↗</span></a>'


chapters = [
('01','pipeline','CPU / GPU 流水线','先分清“调用返回”“提交”和“完成”，再讨论并行。',
 ['Immediate Context 的调用发生在 CPU；命令经运行时与驱动缓冲后由 GPU 执行。','第 N 帧 CPU 准备可以与先前已提交的 GPU 工作重叠；每帧等待会减少这种机会。','Present 和 Flush 推动执行，但函数返回不提供 GPU 工作全部完成的保证。'],
 [('Options::cpuMs','毫秒，默认 0','模拟 CPU 忙碌时长；与 Metrics::cpuMs 的实测输出同名但含义不同。'),('Options::gpuRepeats','次数，默认 1','重复 Draw 的数量，增加 GPU 工作也增加部分 CPU 提交成本。'),('work','volatile uint32_t','防止模拟忙碌循环成为无用代码；不是粒子模拟状态。')],
 'CPU 工作 → Draw × repeats → [模式 1：WaitIdle] → Present → 下一帧',
 'mode 0：正常异步执行；mode 1：每帧等待 GPU。两者绘制内容相同。',
 'T串行 = C + G；T稳态理想 ≈ max(C, G)',
 'C = 6 ms，G = 10 ms：串行 16 ms（62.5 FPS），理想流水线稳态间隔 10 ms（100 FPS）。首帧仍要经过两个阶段；输入到显示延迟没有因此变成 10 ms。',
 'Pipeline::Frame','01_pipeline.cpp','--cpu-ms 4 --gpu-repeat 32 --mode 0',
 '为什么单个 CPU 渲染线程也能让 CPU/GPU 同时工作？',
 '因为 CPU 和 GPU 是不同处理器，命令提交与执行异步；不需要先增加 CPU 线程才能产生两者的重叠。'),
('02','timing','正确测量 GPU','用两套时钟回答两个不同的问题。',
 ['CPU 时钟测 Draw 调用和其他 CPU 工作；GPU 时间戳测 GPU 时间线上的区间。','先录制 Query，再在后续帧取结果。S_FALSE 是尚未完成，不是“耗时为零”。','检查 Disjoint 和 Frequency，按源帧写回结果；槽满时跳过采样而不等待。'],
 [('Slot::begin / end','TIMESTAMP Query','每个槽两枚时间戳，不能拿 CPU 时钟与它们相减。'),('Slot::disjoint','TIMESTAMP_DISJOINT Query','提供时间戳有效性和 GPU 计数频率。'),('Slot::frame / pending','源帧号 / 是否在途','frame 用于 CSV 归属；pending 阻止覆盖未完成 Query。'),('GpuTimer::active_ / dropped','槽指针 / 计数','active_ 为 nullptr 表示本帧未采样；dropped 记录因满环而丢弃的采样。')],
 'Poll 旧 Query → Begin(disjoint) → End(start) → Frame → End(stop) → End(disjoint)',
 'mode 0：延后取时间戳；mode 1：每帧强制等待作反例。查询环本身始终使用同一个实现。',
 'T_GPU(ms) = 1000 × (B − A) / F',
 'A = 1,000,000 tick，B = 1,250,000 tick，F = 1,000,000,000 tick/s，则 T = 0.25 ms。只在 Disjoint = false 且 F > 0 时有效。',
 'GpuTimer::Poll','02_timing.cpp','--gpu-repeat 32 --mode 0',
 'UI 的 CPU 和 GPU 均值为什么有效样本数不同？',
 'UI 显示最近 10 秒运行时间内的逐帧算术均值。GPU 结果延后返回，按源帧归入窗口；未返回或已过期的结果不参与均值。CSV 仍保留逐帧数据。'),
('03','upload','动态缓冲上传','把 GPU 还在使用的区间和 CPU 要写的区间分开。',
 ['WRITE_DISCARD 使旧逻辑内容作废，运行时可通过资源重命名避免覆盖旧数据。','WRITE_NO_OVERWRITE 是应用作出的“不覆盖在用数据”承诺，不是自动等待或自动分配器。','两种模式都按 1024 粒子一批绘制，保证对照的粒子数和 Draw 数相同。'],
 [('first / n','元素下标 / 当前批次元素数','first 每次加 1024；n = min(1024, total − first)，处理不足整批的尾部。'),('destination','元素偏移','stream 模式为 first；每批 DISCARD 模式为 0。指针加法以 Particle 为单位。'),('kind','D3D11_MAP','首批 DISCARD；stream 后续批次才使用 NO_OVERWRITE。'),('mapped.pData / capacity_','映射指针 / 容量','pData 只能在成功 Map 到 Unmap 之间访问；容量单位是粒子数。')],
 'Map → memcpy(Particle × n) → Unmap → Draw(n, destination)',
 'mode 0：每批 DISCARD，写到偏移 0；mode 1：首批 DISCARD，后续 NO_OVERWRITE，偏移只向前。',
 '字节数 = N × 16；Draw 次数 = ⌈N / 1024⌉',
 'N = 10,003 时，缓冲为 160,048 B，共 10 次 Draw；最后一批从元素 9,216 开始，有 787 个元素，stream 字节偏移为 147,456 B。',
 'ParticleRenderer::Cpu','03_upload.cpp','--count 262144 --mode 1',
 '为什么最后一批结束后不能直接回到偏移 0 继续 NO_OVERWRITE？',
 '先前的 Draw 可能尚未读取完偏移 0 的内容。本例下一帧重新 DISCARD；更复杂的回绕分配器需要完成状态管理。'),
('04','cpu-jobs','CPU 任务并行','任务独立、区间互斥、汇合明确，才有正确的并行。',
 ['固定线程池避免每帧创建线程；任务只更新 CPU 数组，不触碰 Immediate Context。','整数分区覆盖 [0, N)，每个 worker 写自己的连续区间。','Run 会等待全部任务完成后返回，此时主线程才可以安全上传。'],
 [('worker / Jobs::Size()','工作线程编号 / 总数','worker 从 0 到 W−1；W 来自 --workers，范围 1..32。'),('first / last','左闭右开下标','first = floor(N×i/W)，last = floor(N×(i+1)/W)。'),('generation_ / observed','批次代数','worker 用于识别新任务；配合条件变量谓词防止虚假唤醒误执行。'),('remaining_ / done_','剩余任务数 / 汇合通知','每个 worker 完成后在锁内递减；归零唤醒提交线程。')],
 'Run 派发 → 多个 worker 更新各自区间 → Run 汇合 → Cpu 上传绘制',
 'mode 0：串行更新；mode 1：固定线程池并行更新。算法、上传、绘制保持一致。',
 'firstᵢ = ⌊Ni/W⌋；lastᵢ = ⌊N(i+1)/W⌋',
 'N = 10,003，W = 3：区间依次 [0,3334)、[3334,6668)、[6668,10003)，长度 3334、3334、3335。',
 'Jobs::Run','04_cpu_jobs.cpp','--count 1048576 --workers 4 --mode 1',
 '独立数据已经可以并行，为什么并行版本仍可能更慢？',
 '线程唤醒、任务分发、汇合和带宽都有成本。小数据规模时这些开销可能超过节省的计算时间。'),
('05','compute','GPU 粒子计算','让更新结果直接进入绘制，理解 GPU 数据驻留。',
 ['CPU Particle 与 HLSL Particle 的布局对应，StructuredBuffer 的 stride 明确为 16 B。','每个 HLSL 线程只更新一个粒子；没有组内交换，不需要原子和组内同步。','清空冲突的 SRV/UAV 绑定后切换计算与绘制；解除绑定不等于 CPU 等待。'],
 [('count / particleCount','有效粒子数','CPU 写常量，GPU 用它拦住最后一个工作组的多余线程。'),('dt / deltaTime','秒/模拟步','固定 1/60 s，速度乘 dt 得到本次位置增量。'),('id.x','全局逻辑线程编号','第一个线程为 0，最终有效线程为 count−1。'),('buffer / srv / uav','底层存储 / 读视图 / 写视图','SRV、UAV 是同一个 buffer 的不同访问视图，不是两份粒子副本。')],
 'ConstantsFor → CS 绑定 → Dispatch → 清空 UAV → VS 绑定 SRV → Draw → 清空 SRV',
 'mode 0：GPU 更新并驻留绘制；mode 1：CPU 更新后上传。比较整段时间，不能只比 Dispatch 调用开销。',
 'groups = ⌈N / 256⌉ = (N + 255) / 256（整数除法）',
 'N = 10,003：40 组 × 256 = 10,240 个逻辑线程，237 个线程因越界保护直接返回。256 是当前 HLSL 组大小，不是物理 GPU 核心数。',
 'ComputeParticles::Step','05_compute.cpp','--count 10003 --mode 0 --validate',
 '为什么把组大小改成 128 时必须同时改 C++？',
 'CPU 的 Dispatch 参数是“组的数量”，不是线程数。HLSL numthreads 与 CPU 向上取整公式必须使用相同的每组线程数。'),
('06','readback','GPU 结果回读','先确认完成，再读取；满环时明确处理积压。',
 ['CopyResource 只安排复制；立即 Map READ 可能让 CPU 等待复制及其依赖。','异步路径按 EVENT 查询完成后 Map DO_NOT_WAIT；未就绪就留给后续帧。','4 槽全部在途时跳过新采样，不覆盖旧槽，也不强制排空。'],
 [('Slot::staging / complete','CPU 可读资源 / EVENT','一槽一资源一查询，才能独立管理正在复制的结果。'),('pending / frame','是否在途 / 复制源帧','只有 pending=false 的槽允许放入新复制。'),('latestFrame / consumed','最新结果源帧 / 已消费样本数','遍历槽的顺序不一定是时间顺序；保留最大源帧结果。'),('baseFrame_ / steps_','模式重置基准 / 更新次数','M 切模式后全局 frame 不归零，因此历史验证必须减去新基准。')],
 'Poll 旧槽 → 若完成则读取并释放 → Enqueue 新复制 → End(EVENT) → 后续帧再 Poll',
 'mode 0：查询完成后读取；mode 1：每帧立即 Map。异步环满时会跳采样，消费频率不保证相同。',
 '数据帧龄 = 当前 frame − latestFrame',
 '当前帧 120 读取到源帧 117，帧龄是 3。它不是自动等于 50 ms：只有真实帧间隔稳定为 1/60 s，才可作这种时间近似。',
 'ReadbackRing::Poll','06_readback.cpp','--count 262144 --mode 0',
 '多缓冲以后，只要过两帧就一定可以读了吗？',
 '不能。帧数是调度经验，完成查询才是正确性依据；GPU 负载和队列深度随时会变化。'),
('07','deferred','多线程命令录制','把 CPU 录制成本拆开，保留一个明确的回放入口。',
 ['每个 worker 独占一个 Deferred Context；渲染线程独占 Immediate Context。','录制前明确绑定全部必要状态，不能假定继承 Immediate 的状态。','FinishCommandList 完成 CPU 录制；ExecuteCommandList 在 Immediate 顺序回放。'],
 [('contexts_[worker] / lists_[worker]','独占录制上下文 / 命令列表','一个 worker 对应一项；共享的只读资源由多个列表引用。'),('draws / first / last','Draw 数 / 录制区间','沿用第 4 章的分区公式，对 Draw 集合而非粒子更新分区。'),('side / cell / corner','网格边长 / 单元号 / 顶点号','side=ceil(sqrt(draws))；每个单元 3 个顶点。'),('DriverCommandLists / DriverConcurrentCreates','驱动能力布尔值','从 CheckFeatureSupport 读取，不根据显卡品牌推断。')],
 '并行：ClearState → 绑定 → Draw → FinishCommandList；汇合后：顺序 ExecuteCommandList',
 'mode 0：Immediate 单线程录制；mode 1：Deferred 多线程录制。绘制相同静态网格。',
 'T录制近似 = max(Tworkerᵢ) + T派发汇合 + T回放',
 '这个式子是分析模型，不是程序计算出来的精确分项。即便录制被分摊，回放、状态、驱动和调度成本仍可能限制收益。',
 'Deferred::Frame','07_deferred.cpp','--draws 16384 --workers 4 --mode 1',
 '为什么这不是 DX12 的多 GPU 队列？',
 '这里多个线程生成 CPU 端命令列表，最终仍经 Immediate Context 形成提交工作；应用没有建立独立 graphics/compute/copy 队列。'),
('08','integrated','组织游戏的一帧','把依赖、吞吐量和允许的数据延迟一起考虑。',
 ['CPU 对象更新后才能上传绘制；GPU 视觉粒子更新后才能由 SRV 绘制。','两组数据相互独立，可以改变 CPU/GPU 工作安排；先写 Dispatch 不保证驱动立即送达硬件。','综合模式改变多个策略，要回到独立章节隔离每项收益。'],
 [('actors_ / particles_','CPU 对象数组 / GPU 视觉数据','两组初始分布与运动相同，便于验证；各自存储在 CPU 和 GPU。'),('jobs_ / ring_','任务池 / 回读环','分别处理 CPU 更新和可延迟的结果消费。'),('m.updateMs / m.readbackMs','CPU 测量值，毫秒','观察节省在哪一段；GPU 区间必须另读 Query。'),('simulated / baseFrame_','重置后步数 / 全局帧基准','最终验证按模拟步数，历史回读按源帧相对基准重放。')],
 'GPU 更新绘制 ↔ 独立 CPU 对象更新 → CPU 上传绘制；统计复制 → 以后消费',
 'mode 0：并行组织 + 延后回读；mode 1：串行更新 + 立即回读。每帧两次绘制；回读环满时可跳采样。',
 '加速比 S = T基线 / T改进；节省比例 = 1 − T改进 / T基线',
 '某个可比区间从 8 ms 降到 5 ms：S=1.6，节省 37.5%。只能对相同定义的测量区间比较；不能拿 update_ms 与整帧延迟互相比。',
 'Integrated::Frame','08_integrated.cpp','--count 262144 --workers 4 --mode 0',
 'GPU 工作缩短后 FPS 没提高，应该先看什么？',
 '查看 CPU 更新、上传、回读等待、Present 和 VSync 是否成为限制；检查优化是否真的缩短了关键依赖路径。')]

# Fields documented from classroom.h and actual use sites, including currently unused fields.
variables = [
('Particle','x, y','float；归一化位置','初始范围 [0,1)，每次按速度积分并周期回绕。CPU/GPU 各保留自己的数据。','src/common/classroom.h'),
('Particle','vx, vy','float；位置单位/s','初始范围约 [−0.075,0.075)，本例不改变速度。注意单位不是“像素/帧”。','src/common/classroom.h'),
('Constants ↔ HLSL','count ↔ particleCount','uint32_t / uint；偏移 0 B','有效元素数量；作为 GPU 越界保护的上界。','src/common/classroom.h'),
('Constants ↔ HLSL','dt ↔ deltaTime','float；偏移 4 B；秒','固定 1/60。只表示模拟时间步，不表示真实执行每帧一定 16.67 ms。','src/common/classroom.h'),
('Constants ↔ HLSL','time ↔ elapsedTime','float；偏移 8 B','目前 ConstantsFor 写入 0，当前 HLSL 没有使用。不是已经实现的动画计时参数。','src/common/classroom.h'),
('Constants ↔ HLSL','grid ↔ gridSize','uint32_t / uint；偏移 12 B','目前写入默认 64，当前 HLSL 没有使用。实际三角形网格在 CPU 的 InitGrid 中生成。','src/common/classroom.h'),
('Options','count, draws, workers','uint32_t；默认 65536 / 4096 / 4','count=1..1048576，draws=1..65536，workers=1..32。分别是粒子数量、网格 Draw 数、CPU worker 数。','src/common/classroom.h'),
('Options','frames, mode','uint32_t；默认 0 / 0','frames=0：窗口持续运行，离屏转换为 120。mode 的 0/1 含义每章不同。','src/common/classroom.h'),
('Options','cpuMs, gpuRepeats','uint32_t；默认 0 ms / 1 次','cpuMs 是第 1 章模拟负载；gpuRepeats 仅第 1、2 章使用。','src/common/classroom.h'),
('Options','headless, warp','bool；默认 false','headless 渲染到离屏目标；warp 使用软件设备，不适合作为真实 GPU 性能数据。','src/common/classroom.h'),
('Options','debug, validate, vsync','bool；默认 false / false / true','debug 请求 Debug Layer；validate 在采样结束后检查；vsync 决定窗口 Present 的同步间隔。','src/common/classroom.h'),
('Options','output, capture','filesystem::path；默认空','output 写 CSV 及 JSON；capture 写最后的 DX11 视口 PNG，不含左侧 GDI 教学面板。','src/common/classroom.h'),
('Metrics','frame, gpuFrame','uint64_t / int64_t；帧号','frame 从 0 递增；gpuFrame 缺失为 −1。CSV 中有效 GPU 样本归属本行 frame。','src/common/classroom.h'),
('Metrics','cpuMs, presentMs','double；毫秒','cpuMs 包含清屏与本章 CPU 工作、计时命令；presentMs 包含窗口复制/Present 提交或离屏 Flush。','src/common/classroom.h'),
('Metrics','gpuMs','double；毫秒；初值 −1','GPU 时间戳区间，不是 GPU 各硬件单元忙碌时间。−1 表示未取得有效样本。','src/common/classroom.h'),
('Metrics','updateMs, uploadMs, readbackMs','double；毫秒','分别记录本章更新/录制、粒子动态顶点上传、CPU 回读处理；不是可直接相加的所有 GPU 成本。','src/common/classroom.h'),
('Metrics','drawCalls, droppedReadbacks','uint32_t；本帧计数','实际 Draw 次数；由于回读环满而未提交的新采样数。它们不是累计全程统计。','src/common/classroom.h'),
('Metrics','readbackAge','int64_t；帧；初值 −1','当前帧减最新已消费源帧，−1 表示没有结果或本章不使用回读。','src/common/classroom.h'),
('Device','device, context','ComPtr；CPU 接口对象','device 创建资源；context 是唯一 Immediate Context，在本工程只由渲染线程调用。','src/common/classroom.h'),
('Device','target, rtv','纹理 / RenderTargetView','持久的 800×640 BGRA 渲染纹理及其输出视图。保证 Present 后仍有可验证的最后画面。','src/common/classroom.h'),
('Device','swapchain, swapBuffer','交换链 / 后台纹理','窗口模式才创建；Present 前从 target 复制。离屏模式没有交换链。','src/common/classroom.h'),
('Device','infoQueue, debugActive, adapter','诊断接口 / bool / 名称','用于 Debug Layer 诊断和报告所选 GPU。硬件默认选择专用显存最大的非软件适配器。','src/common/classroom.h'),
('GpuTimer','slots_[16], active_','查询槽数组 / 当前槽指针','每槽包含两枚时间戳和一枚 Disjoint；未完成槽不能复用。','src/common/classroom.h'),
('GpuTimer','dropped','uint32_t；累计帧数','因 16 个槽都在途而跳过本帧计时。与回读的 droppedReadbacks 是两类不同丢样。','src/common/classroom.h'),
('Jobs','threads_, task_','线程数组 / std::function','固定 worker 执行本批 task(workerIndex)。任务必须在 Run 返回前全部完成。','src/common/classroom.h'),
('Jobs','mutex_, work_, done_','互斥量 / 条件变量','保护批次状态、唤醒 worker、通知主线程汇合。不是拿来串行锁住整个粒子更新循环。','src/common/classroom.h'),
('Jobs','generation_, remaining_, stop_, error_','代数 / 余数 / 停止标记 / 异常','generation 标识新批次；remaining 归零时完成；异常在汇合后重新抛出。','src/common/classroom.h'),
('ParticleRenderer','dynamic_, constants_, gridVertices_','GPU Buffer 接口','分别用于频繁顶点上传、16 B 常量和第 7 章静态三角形网格。','src/common/classroom.h'),
('ParticleRenderer','cpuVS_, gpuVS_, pixel_, layout_','着色器 / 输入布局','CPU 路径通过顶点输入读取；GPU 路径通过 SV_VertexID 读取 SRV；二者共用像素着色器。','src/common/classroom.h'),
('ComputeParticles','buffer, srv, uav, shader, count','GPU 粒子资源及数量','buffer 底层存储一次；srv/uav 引用它。shader 是 UpdateCS，count 决定分配与派发。','src/common/classroom.h'),
('ReadbackRing','slots_[4], count_','4 个 staging 槽 / 元素数','额外 staging 数据容量约为 4×N×16 B，不包含 Query 和驱动内部开销。','src/common/classroom.h'),
('ReadbackRing','latest, latestFrame, consumed','CPU 数组 / 源帧 / 累计样本数','latest 是已消费的最近数据；latestFrame=0 不能单独判定“没有数据”，需要看 latest.empty()。','src/common/classroom.h'),
('主循环 / UI','simulated, samples, rolling','本轮步数 / CSV 样本 / 滑动窗口','调参后重置模拟、CSV 统计段和 10 秒滑动均值。GPU 结果按源帧回填，缺失值不参与平均；暂停不消耗统计窗口时间。','src/common/app.cpp'),
('WindowState','shown','uint64_t；初始化 0','当前实现未读取或递增，属于未使用字段；不要把它理解为屏幕已呈现帧数。','src/common/app.cpp'),
('HLSL','SV_DispatchThreadID / SV_VertexID','系统输入语义','前者提供全局计算线程编号，后者用于本例 Draw(count,0) 从粒子 SRV 读取元素。','shaders/particles.hlsl'),
('HLSL','SV_POSITION / SV_TARGET','系统输出语义','顶点输出的齐次裁剪空间位置；像素输出写入 render target。本例 w=1。','shaders/particles.hlsl')]

# name, input/output, behavior, threading/waiting, source path, source search text.
functions = [
('Run','argc/argv + unique_ptr<Lesson> → int 退出码','创建设备与课件，驱动每帧循环，最后验证和导出。','CPU 渲染线程；结束时显式 WaitIdle。','src/common/app.cpp','int Run('),
('Parse','命令行 → Options','检查数字范围，离屏默认帧数设为 120。','CPU 启动阶段；内部函数。','src/common/app.cpp','Options Parse('),
('Percentile','数值数组 + p → double','排序后取 floor((n−1)p) 下标，不做插值；空数组返回 −1。','CPU 离线统计；p 由内部调用提供。','src/common/app.cpp','double Percentile('),
('WriteResults','Options/样本/诊断 → CSV + JSON 文件','排除 min(30,frames/4) 个预热帧；GPU 的 −1 不参加统计。','CPU 退出阶段，有文件 I/O。','src/common/app.cpp','void WriteResults('),
('Procedure / Text','Windows 消息 / 绘制参数 → 消息结果 / 无返回值','处理 M、空格、V、Esc；使用 GDI 绘制中文侧栏。','CPU UI 线程；不是 HLSL 或 GPU 计时工作。','src/common/app.cpp','LRESULT CALLBACK Procedure('),
('Lesson::Info','无 → LessonInfo','提供标题、教学目标、知识点、实验提示、模式名和文档位置。','CPU 元数据；每章覆盖此虚函数。','src/common/classroom.h','virtual LessonInfo Info'),
('Lesson::Init','Device + Options → void','创建本章所需资源与初始数据；切模式时再次调用。','CPU 初始化；不计入逐帧实验。','src/common/classroom.h','virtual void Init'),
('Lesson::Frame','Device + Options + Metrics + dt → void','本章的一步更新/绘制；Metrics 用引用回传本帧指标。','调用在渲染线程；内部可派发 CPU 任务或 GPU 命令。','src/common/classroom.h','virtual void Frame'),
('Lesson::Validate','Device + Options + frames → void','需要数值验证的章节覆盖它；默认实现不做数值对照。','采样结束后执行；可以阻塞回读。','src/common/classroom.h','virtual void Validate'),
('Device::Init','可空 HWND + Options → void','创建 FL 11_0 设备、可选交换链、独立渲染目标和诊断接口。','初始化；失败通过 Check 抛异常。','src/common/gpu.cpp','void Device::Init('),
('Device::Begin / BindTarget','无 / 指定 Context → void','绑定 RTV 和 viewport；Begin 还清屏。','CPU 录入 GPU 命令；BindTarget 可用于独占 Deferred Context。','src/common/gpu.cpp','void Device::BindTarget('),
('Device::Present','vsync bool → void','窗口复制到 swapBuffer 并 Present；离屏 Flush。','Present 可能因呈现节奏/排队阻塞；Flush 不保证完成。','src/common/gpu.cpp','void Device::Present('),
('Device::WaitIdle','无 → void','End EVENT + Flush，直到查询完成，15 秒超时失败。','明确的 CPU 等待；每次查询间睡眠约 1 ms。','src/common/gpu.cpp','void Device::WaitIdle('),
('Device::Compile','文件名 + 入口名 + profile → ID3DBlob','从当前目录或可执行文件附近找 shaders，编译 HLSL。','CPU 启动阶段；使用严格检查与优化级别 3。','src/common/gpu.cpp','ComPtr<ID3DBlob> Device::Compile('),
('Device::Capture','PNG 路径 → void','读取持久 target，按 RowPitch 复制行，通过 WIC 写 PNG。','采样后阻塞；导出的是 DX11 视口。','src/common/gpu.cpp','void Device::Capture('),
('Device::ValidateTarget / DebugErrors','无 → void / uint32_t','检查可见像素及 hash；打印并计数 Debug Layer 警告和错误。','退出阶段；图像检查会同步回读。','src/common/gpu.cpp','void Device::ValidateTarget('),
('GpuTimer::Init','Device → void','预分配 16 组 timestamp/disjoint 查询对象。','CPU 初始化。','src/common/gpu.cpp','void GpuTimer::Init('),
('GpuTimer::Begin / End','Device + 源帧 / Device → void','选空槽并录入前后时间戳；满环时 Begin 跳过计时。','CPU 发出 GPU Query 命令，不当场取结果。','src/common/gpu.cpp','void GpuTimer::Begin('),
('GpuTimer::Poll','Device + consume(frame,ms) → void','检查三份 Query 数据，换算毫秒并调用回调，释放完成槽。','DONOTFLUSH 查询；S_FALSE 立即留给以后，不忙等。','src/common/gpu.cpp','void GpuTimer::Poll('),
('Jobs::Jobs / ~Jobs','线程数 → 构造 / 析构','建立持久 worker；析构设置停止标记，唤醒并 join。','CPU；析构等待 worker 退出。','src/common/gpu.cpp','Jobs::Jobs('),
('Jobs::Run','task(workerIndex) → void','广播一批任务，等待 remaining=0，然后重新抛出 worker 异常。','调用线程在汇合点等待；worker 在锁外执行计算。','src/common/gpu.cpp','void Jobs::Run('),
('Jobs::Size','无 → uint32_t','返回固定工作线程数。','CPU 只读查询。','src/common/classroom.h','uint32_t Size()'),
('InitialParticles','count → vector<Particle>','固定种子生成位置和速度，让各模式从一致数据开始。','CPU 初始化；每次相同 count 得到相同序列。','src/common/particles.cpp','std::vector<Particle> InitialParticles('),
('UpdateParticles','数组引用 + [first,last) + dt → void','更新指定区间 p+=v×dt，并执行一次周期回绕。','CPU；互斥区间可并行。要求有效下标和足够小时间步。','src/common/particles.cpp','void UpdateParticles('),
('VerifyParticles','actual + expected → void','检查数量、位置有限/范围和最大误差，超阈值抛异常。','CPU 验证阶段；位置用周期距离，速度用绝对误差。','src/common/particles.cpp','void VerifyParticles('),
('ParticleRenderer::Init','Device + capacity → void','创建 VS/PS、输入布局、动态顶点缓冲及常量缓冲。','CPU 初始化。','src/common/particles.cpp','void ParticleRenderer::Init('),
('ParticleRenderer::ConstantsFor','count + dt + grid → void','DISCARD 映射 16 B 常量，写入 {count, dt, 0, grid}。','CPU 上传；time/grid 目前没有参与 HLSL 运算。','src/common/particles.cpp','void ParticleRenderer::ConstantsFor('),
('ParticleRenderer::Cpu','CPU 粒子 + Metrics + stream/batched → void','上传顶点并绘制；batched=true 时每批 1024 个元素。','CPU 发命令；Map/资源压力可能带来等待。','src/common/particles.cpp','void ParticleRenderer::Cpu('),
('ParticleRenderer::Gpu','SRV + count + Metrics → void','VS 从 SRV 取粒子，Draw(count,0)，随后清空 VS 的 SRV 槽。','CPU 录入绘制；不把粒子回读 CPU。','src/common/particles.cpp','void ParticleRenderer::Gpu('),
('ParticleRenderer::InitGrid','draws → void','在 CPU 建立 3×draws 个顶点，创建 immutable 顶点缓冲。','CPU 初始化。','src/common/particles.cpp','void ParticleRenderer::InitGrid('),
('ParticleRenderer::GridState / Grid','Context / Device+draws → void','GridState 绑定完整网格状态；Grid 顺序发出 Draw(3,i×3)。','GridState 用于各独占 Context；Grid 使用 Immediate。','src/common/particles.cpp','void ParticleRenderer::GridState('),
('ComputeParticles::Init','Device + n → void','创建 DEFAULT structured buffer 及 SRV/UAV，编译 UpdateCS。','CPU 初始化；初始数组一次传入 GPU。','src/common/particles.cpp','void ComputeParticles::Init('),
('ComputeParticles::Step','Device + Renderer + dt → void','更新常量、绑定 CS/UAV，派发 ceil(count/256) 组，解除 UAV。','函数在 CPU；UpdateCS 在 GPU 异步执行。','src/common/particles.cpp','void ComputeParticles::Step('),
('ComputeParticles::ReadBlocking','Device → vector<Particle>','临时创建 staging，复制并立即 Map，取得全部粒子。','明确阻塞；用于结束验证，不是第 6 章逐帧基线。','src/common/particles.cpp','std::vector<Particle> ComputeParticles::ReadBlocking('),
('ReadbackRing::Init','source buffer + count → void','预分配四个 staging 和 EVENT；清空历史结果。','CPU 初始化。','src/common/particles.cpp','void ReadbackRing::Init('),
('ReadbackRing::Enqueue','source + frame + Metrics → void','空槽复制并 End(EVENT)，标记 pending；满环增加丢采样计数。','CPU 录入复制；不等待空槽。','src/common/particles.cpp','void ReadbackRing::Enqueue('),
('ReadbackRing::Poll','当前 frame + Metrics → void','完成查询后非阻塞 Map，拷到 CPU latest，保留最新源帧。','不强制等待；复制内存仍有 CPU 成本。','src/common/particles.cpp','void ReadbackRing::Poll('),
('Check / Ms / Wide / Utf8','HRESULT / 起点 / 字符串 → void / ms / 转码文本','Check 仅对 FAILED(hr) 抛异常；S_FALSE 不是失败。Ms 读取 CPU steady_clock。','CPU 工具函数，均不测量 GPU 时间。','src/common/classroom.h','inline double Ms('),
('HLSL MakeParticle','float2 位置 → VertexOutput','把 [0,1) 的坐标映射到裁剪空间，按位置计算颜色。','GPU 顶点阶段中的辅助函数；w=1。','shaders/particles.hlsl','VertexOutput MakeParticle('),
('HLSL CpuVS / GpuVS','顶点 float4 / SV_VertexID → VertexOutput','CpuVS 取顶点 xy；GpuVS 读取 particles[index].position。','GPU 顶点阶段；两者复用 MakeParticle。','shaders/particles.hlsl','VertexOutput CpuVS('),
('HLSL PS','VertexOutput → float4 SV_TARGET','输出插值颜色，alpha 固定为 1。','GPU 像素阶段。','shaders/particles.hlsl','float4 PS('),
('HLSL UpdateCS','SV_DispatchThreadID → UAV 写入','越界线程返回；其余线程独占一个粒子完成更新。','GPU 计算阶段；每组 256 个逻辑线程。','shaders/particles.hlsl','void UpdateCS(')]

formulas = [
('帧流水线','T串行 = C + G；T稳态 ≈ max(C,G)；FPS ≈ 1000 / T(ms)','C/G 为每帧 CPU/GPU 阶段毫秒。只适用于充分重叠、无额外节流和依赖成本的理想两阶段模型。','6 + 10 = 16 ms；max(6,10) = 10 ms。不是端到端延迟公式。'),
('GPU 时间戳','T(ms) = 1000 × (B − A) / F','A/B 是同一有效区间的 tick，F 单位为 tick/s；Disjoint=false、F>0。不能混合 CPU 时间戳。','Δ=250000 tick，F=10⁹ tick/s → 0.25 ms。'),
('CPU 分区','firstᵢ=⌊Ni/W⌋；lastᵢ=⌊N(i+1)/W⌋','N 为元素数，W 为线程数，i∈[0,W)。区间左闭右开，连续且不重叠。','10003 / 3 → 3334、3334、3335。'),
('GPU 工作组','G=⌈N/B⌉=⌊(N+B−1)/B⌋；多余线程=G×B−N','N 为粒子数，B 为每组逻辑线程数。工程当前 B=256。整数运算前须保证加法不溢出。','N=10003、B=256 → G=40，多余 237。'),
('粒子积分与回绕','p′=p+vΔt；p″=p′−⌊p′⌋（理想周期映射）','p 为归一化位置，v 为位置/s，Δt 为秒。工程用 if 加减一次实现，在小步长和给定速度下等价；大步长跨越多圈不再等价。','p=0.9995、v=0.06、Δt=1/60 → p′=1.0005 → p″=0.0005。'),
('数据量与偏移','缓冲 B=16N；字节偏移=16×元素偏移；MiB=B/2²⁰','Particle 是四个 32-bit float，stride=16 B。这里的 B 指字节数，不是上一式的每组线程数。','65536 个粒子=1048576 B=1 MiB；4 个 staging 数据区约 4 MiB。'),
('上传批次与绘制','Draw 数=⌈N/1024⌉；n=min(1024,N−first)','仅指第 3 章分块上传路径；一般 Cpu/Gpu 粒子路径一次 Draw。','10003 个粒子 → 10 次 Draw，尾批 787。'),
('回读年龄与历史参考','age=frame−latestFrame；历史步数=latestFrame−baseFrame+1','源帧按更新后的状态复制；因此源帧 0 已经包含第一次更新。切模式后用新基准。','base=100、latest=117 → 历史参考执行 18 步。'),
('最大允许误差','e位置=min(|a−b|,1−|a−b|)；e速度=|va−vb|','位置在 [0,1) 的周期域；全体位置/速度误差取最大值。本工程阈值为 0.0002。','0.99999 与 0.00001 的周期距离为 0.00002，不是 0.99998。'),
('加速比与串行上限','S=T旧/T新；Amdahl：S(W)=1/((1−p)+p/W)','p 是基线中可并行部分比例，W 是理想并行度；忽略调度、通信和带宽约束，真实结果可能更低。','p=0.75、W=4 → 2.286 倍；W→∞ 的上限为 4 倍。'),
('网格与坐标','side=⌈√D⌉；列=cell mod side；行=⌊cell/side⌋','D 为三角形 Draw 数，每单元 3 个顶点；最终 NDC xy=1.9×p−0.95。','D=257 → side=17；第 256 个单元位于列 1、行 15（从 0 开始）。'),
('本工程分位数','Qp=排序数组[⌊(n−1)p⌋]；预热=min(30,⌊frames/4⌋)','这是实现采用的下标法，不是插值分位数。偶数样本中位数取下侧，不平均中间两个值。','[1,2,3,4] 的 p=0.5 → 下标 1 → 2。')]


def render_chapters():
    result=[]
    for n,key,title,intro,points,vars_,flow,mode,formula,example,focus,file,args,question,answer in chapters:
        path='src/chapters/'+file
        links=srclink(path,label='本章源码')+' · <a href="'+file.replace('.cpp','.md')+'">原章节教材 ↗</a>'
        if '::' in focus and not focus.startswith(('Pipeline','Deferred','Integrated')):
            impl='src/common/gpu.cpp' if focus.startswith(('GpuTimer','Jobs')) else 'src/common/particles.cpp'
            links+=' · '+srclink(impl,focus+'(',focus)
        result.append(f'''<section class="chapter searchable" id="ch{n}" data-title="{n} {esc(title)}">
<div class="section-top"><span class="number">{n}</span><div><p class="eyebrow">CHAPTER / {n}</p><h2>{esc(title)}</h2><p class="lead">{esc(intro)}</p></div></div>
<h3>本章教什么</h3><ol class="teach">{''.join('<li>'+esc(p)+'</li>' for p in points)}</ol>
<div class="flow" role="note">{esc(flow)}</div><p><strong>对照模式：</strong>{esc(mode)}</p>
{render_walkthrough(n,path,SOURCE_TEXT[path],esc,block,srclink,SOURCE_TEXT)}
<h3>随时回查：本章变量</h3>{table(['变量 / 状态','类型、单位或默认值','为什么需要它'],[(code(a),esc(b),esc(c)) for a,b,c in vars_])}
<div class="formula"><span>本章公式</span><p>{esc(formula)}</p><small>{esc(example)}</small></div>
<h3>直接在 GUI 中做本章实验</h3><p>双击 <code>build/Release/{file.replace('.cpp','.exe')}</code> 打开本章。窗口左侧的下拉框切换两种模式，下方数字框修改本章参数。输入完成停顿 0.6 秒自动生效，也可点“立即应用”。“暂停 / 继续”和“垂直同步”可直接点击，不需要输入命令行。参数越界时会提示允许范围，并保留上一次有效值。</p><p>每次应用新参数会安全等待 GPU，重建资源，并清空当前模拟与统计；这是调参过程，不计入正常帧耗时。暂停时也能修改参数，点击继续后显示新画面。指标显示最近 10 秒运行时间内的逐帧算术均值，不足 10 秒时使用已有样本；暂停冻结，继续后接着累计。CPU/GPU 显示各自有效样本数，缺失项显示破折号，GPU 按源帧归属；回读帧龄只平均有效值，丢样显示每帧平均次数。CSV 仍保留逐帧原始值。只显示本章使用的参数：粒子数、工作线程、Draw 次数、CPU 负载或 GPU 重复次数。模式 0 并不总代表更快，先读上方的模式说明。</p><p class="links">{links}</p><details class="exercise"><summary>可选：自动采样与 CSV 导出命令（批量测试时使用）</summary><p>日常学习直接使用 GUI。需要保存固定参数的对照数据时，可分别运行以下命令。若运行中调参，CSV/JSON 仅保留最后一组参数的统计段。</p>{block('\n'.join('.\\build\\Release\\'+file.replace('.cpp','.exe')+' '+re.sub(r'--mode\s+\d+','',args).strip()+' --mode '+str(mode_id)+' --frames 180 --no-vsync --validate --output out\\'+n+'-mode'+str(mode_id)+'.csv' for mode_id in (0,1)),'PowerShell · 可选批量采样')}</details>
<details class="exercise"><summary>自测：{esc(question)}</summary><p>{esc(answer)}</p></details></section>''')
    return '\n'.join(result)


def render_variables():
    def variable_link(scope, name, path):
        lines = SOURCE_TEXT[path].splitlines()
        owner = 'Constants' if scope.startswith('Constants') else scope
        member = name.split(',')[0].split(' ↔ ')[0].strip().split('[')[0]
        start = next((i for i,s in enumerate(lines) if re.search(r'\b'+re.escape(owner)+r'\s*\{',s)),0)
        if scope == 'HLSL':
            member = 'SV_DispatchThreadID' if 'Dispatch' in name else 'SV_POSITION'
        if scope == '主循环 / UI':
            member = 'simulated'
        line = next((i+1 for i in range(start,len(lines)) if re.search(r'\b'+re.escape(member)+r'\b',lines[i])),start+1)
        return f'<a class="source-link" href="#{source_id(path)}-L{line}">数据声明 / 使用位置 ↗</a>'
    return ''.join(f'<article class="reference searchable" id="var-{i}" data-title="{esc(scope+" · "+name,quote=True)}"><div class="ref-head"><span class="scope">{esc(scope)}</span><h3>{code(name)}</h3></div><p class="type">{esc(kind)}</p><p>{esc(desc)}</p>{variable_link(scope,name,path)}</article>' for i,(scope,name,kind,desc,path) in enumerate(variables))


def render_functions():
    return ''.join(f'<article class="function searchable" id="fn-{i}" data-title="{esc(name,quote=True)}"><h3>{code(name)}</h3><dl><dt>输入 → 输出</dt><dd>{esc(io)}</dd><dt>做什么</dt><dd>{esc(desc)}</dd><dt>谁执行 / 是否等待</dt><dd>{esc(wait)}</dd></dl>{srclink(path,needle,"跳到对应源码")}</article>' for i,(name,io,desc,wait,path,needle) in enumerate(functions))


def render_sources():
    result=[]
    for path in SOURCES:
        sid=source_id(path)
        lines=''.join(f'<span class="source-line" id="{sid}-L{i}"><span class="line-no" aria-hidden="true">{i:>4}</span>{esc(s)}</span>' for i,s in enumerate(SOURCE_TEXT[path].splitlines(),1))
        result.append(f'<details class="source-file" id="{sid}"><summary>{esc(path)} <small>{len(SOURCE_TEXT[path].splitlines())} 行</small></summary><p><a href="../{path}">打开原始文件 ↗</a> · 下方是生成手册时的源码快照。</p><pre class="source-code"><code>{lines}</code></pre></details>')
    return '\n'.join(result)


STYLE = r'''
.walkthrough{border-top:1px solid var(--border);margin-top:28px;padding-top:24px}.read-step{border-left:3px solid #b8d9cb;padding:2px 18px;margin:22px 0}.read-step h4,.implementation h4{font-size:16px;line-height:1.6;margin:10px 0}.read-step h4 span{color:var(--accent);font-family:Consolas,monospace}.read-step p{margin:8px 0}.implementation{border:1px solid var(--border);border-radius:9px;padding:18px;margin:24px 0;min-width:0}.implementation summary{cursor:pointer;font-weight:650}.implementation .code-block{max-width:100%}
:root{--bg:#f3f5f4;--paper:#fff;--ink:#142c2b;--muted:#586b69;--border:#d9e2df;--accent:#087f6b;--nav:#102c2c;--warm:#f4b45e;--code:#142a30;--shadow:0 12px 35px #173a3210}
*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:35px}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.8 "Segoe UI","Microsoft YaHei UI",sans-serif}a{color:var(--accent);text-underline-offset:4px}button,input,select{font:inherit}button{cursor:pointer}code,pre,.mono{font-family:Consolas,"Cascadia Code",monospace}code{font-size:.9em;overflow-wrap:anywhere}p code,td code,h3 code{background:#eaf1ee;padding:2px 5px;border-radius:4px}pre code{background:none;padding:0;overflow-wrap:normal}h1,h2,h3{line-height:1.35;letter-spacing:-.025em}h1{font-size:clamp(34px,4vw,54px);margin:16px 0 20px}h2{font-size:30px;margin:4px 0 12px}h3{font-size:18px;margin:26px 0 12px}p{margin:12px 0}small{font-size:13px}strong{font-weight:650}.sidebar{position:fixed;inset:0 auto 0 0;width:258px;background:var(--nav);color:#e8f3ef;overflow-y:auto;padding:30px 24px;z-index:10}.brand{color:white;text-decoration:none;font-size:21px;font-weight:700;letter-spacing:.02em;display:block}.brand span{display:inline-grid;place-items:center;border:1px solid #7cad9d;border-radius:10px;width:42px;height:42px;color:#a4ecd0;margin-right:9px}.side-caption{color:#afc7bf;font-size:12px;margin:13px 0 26px;letter-spacing:.15em}.sidebar nav{display:grid;gap:3px}.sidebar nav a{display:block;color:#c1d4cf;text-decoration:none;font-size:14px;padding:9px 10px;border-radius:6px}.sidebar nav a:hover,.sidebar nav a.active{background:#214741;color:#fff}.nav-group{font-size:11px;color:#8faea4;padding:18px 10px 7px;letter-spacing:.15em}.side-foot{border-top:1px solid #36534c;margin-top:26px;padding-top:16px;color:#a4bfb5;font-size:12px}.side-foot a{color:#b9e4d4}.mobile-toggle{display:none}.skip{position:absolute;left:280px;top:-60px;z-index:30;background:white;padding:12px}.skip:focus{top:0}main{margin-left:258px;max-width:1470px;padding:38px clamp(24px,4.6vw,74px) 70px}.toolbar{display:flex;gap:12px;align-items:center;margin-bottom:36px;position:relative}.search-wrap{position:relative;flex:1}.search-wrap input{width:100%;border:1px solid var(--border);border-radius:9px;background:#fff;padding:12px 16px;color:var(--ink)}button.action{border:1px solid var(--border);border-radius:9px;background:white;color:var(--ink);padding:12px 17px;white-space:nowrap}.results{position:absolute;top:100%;left:0;right:0;max-height:460px;overflow:auto;background:#fff;box-shadow:0 20px 50px #122c2b30;border:1px solid var(--border);border-radius:8px;z-index:20;padding:8px}.results a{display:block;text-decoration:none;padding:10px 12px;border-radius:6px}.results a:hover{background:#eaf5ef}.results p{padding:6px 12px;color:var(--muted)}[hidden]{display:none!important}.eyebrow{font:12px/1.5 Consolas,monospace;letter-spacing:.16em;color:var(--accent);margin:0}.hero{position:relative;padding-bottom:32px}.hero .lead{max-width:740px;font-size:18px;color:var(--muted)}.hero-tags{display:flex;flex-wrap:wrap;gap:8px;margin:22px 0}.tag{font-size:12px;background:#e3eeea;border:1px solid #ccdcd4;border-radius:30px;padding:4px 12px}.stats{display:grid;grid-template-columns:repeat(4,1fr);border:1px solid var(--border);background:var(--paper);border-radius:12px;margin:30px 0 0;overflow:hidden}.stat{padding:20px;border-right:1px solid var(--border)}.stat:last-child{border:0}.stat b{font:30px/1.2 Consolas,monospace;color:var(--accent);display:block}.stat span{font-size:13px;color:var(--muted)}.panel,.chapter{background:var(--paper);border:1px solid var(--border);border-radius:14px;padding:clamp(22px,3vw,40px);margin:24px 0;box-shadow:var(--shadow)}.chapter{margin:32px 0}.section-top{display:flex;gap:22px;align-items:flex-start;margin-bottom:25px}.number{font:32px/1.1 Consolas,monospace;background:#e9f4ed;color:var(--accent);border:1px solid #d4e5da;padding:17px;border-radius:12px}.lead{color:var(--muted);margin:0}.teach{padding-left:24px}.teach li{padding:4px 0}.flow{background:#edf5f1;color:#275a50;border-left:4px solid var(--accent);padding:16px 20px;border-radius:0 7px 7px 0;margin:24px 0;line-height:1.9;overflow-wrap:anywhere}.table-scroll{width:100%;overflow:auto;border:1px solid var(--border);border-radius:9px}table{border-collapse:collapse;width:100%;font-size:14px}th{text-align:left;background:#edf3f0;font-weight:650;color:#38574e}td,th{padding:13px 15px;border-bottom:1px solid var(--border);vertical-align:top}tr:last-child td{border-bottom:0}td:first-child{min-width:150px}td{overflow-wrap:anywhere}.formula{background:#fff7e9;border:1px solid #efd9af;border-radius:9px;padding:20px 24px;margin:26px 0}.formula>span{font-size:12px;color:#866428;font-weight:650}.formula>p{font:clamp(16px,1.8vw,21px)/1.6 Consolas,"Microsoft YaHei UI",monospace;color:#573f1c;overflow-wrap:anywhere}.formula>small{display:block;color:#6f603f;line-height:1.8}.code-block{position:relative;border:1px solid #26454b;border-radius:10px;background:var(--code);color:#d9eee6;margin:16px 0;overflow:hidden}.code-label{font:12px/1.4 Consolas,sans-serif;padding:12px 16px;color:#8fbbb2;border-bottom:1px solid #315055}.code-block pre{padding:18px;margin:0;overflow:auto;font-size:14px;line-height:1.8;tab-size:4}.copy{position:absolute;right:8px;top:6px;border:1px solid #4d6c6b;border-radius:5px;background:#264247;color:#d9eee6;font-size:12px;padding:3px 9px}.exercise{border-top:1px dashed var(--border);padding-top:19px;margin-top:24px}.exercise summary{cursor:pointer;font-weight:650}.exercise p{color:var(--muted);padding-left:20px}.reference-grid,.function-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.reference,.function{background:white;border:1px solid var(--border);border-radius:10px;padding:23px;min-width:0}.reference h3,.function h3{margin:8px 0 14px;font-size:17px}.scope{color:var(--accent);font:12px Consolas,sans-serif}.type{font-size:13px;color:var(--muted);border-bottom:1px solid var(--border);padding-bottom:12px}.reference p,.function dd{font-size:14px}.source-link{font-size:13px}.function dl{margin:0}.function dt{font-size:12px;color:var(--muted);margin-top:14px}.function dd{margin:4px 0 16px}.section-heading{margin:60px 0 24px;scroll-margin-top:25px}.formula-bank{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.formula-bank .formula{margin:0}.formula-bank h3{margin:0 0 12px}.call-map{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:18px 0}.call-map span{background:#eaf2ee;border:1px solid #cbdcd3;border-radius:6px;padding:8px 11px;font:13px/1.6 Consolas,monospace}.call-map i{font-style:normal;color:#638278}.note{border-left:3px solid #c8a568;padding:8px 16px;color:#72603d;background:#fff9ef;font-size:14px}.calculators{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:20px}.calculator{border:1px solid var(--border);border-radius:12px;background:white;padding:26px;min-width:0}.calculator h3{margin:0 0 8px}.calculator p{font-size:14px;color:var(--muted)}.calculator label{display:block;font-size:13px;color:var(--muted);margin:12px 0}.calculator input[type=number],.calculator input[type=text],.calculator select{width:100%;padding:9px 11px;border:1px solid #bdcdc5;border-radius:6px;background:white;color:var(--ink);margin-top:5px}.input-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.result-box{background:#edf6f0;border:1px solid #d4e5db;border-radius:7px;padding:14px;margin-top:16px;font-size:14px;white-space:pre-line;overflow-wrap:anywhere}.result-box.error{background:#fff1eb;border-color:#e6b9a3;color:#8a361a}.bar-row{display:grid;grid-template-columns:60px 1fr;align-items:center;font-size:12px;gap:9px;margin:9px 0}.bar-track{height:22px;background:#eef1ef;border-radius:4px;overflow:hidden;display:flex}.bar-cpu{background:#087f6b;height:100%;display:block}.bar-gpu{background:#e1a748;height:100%;display:block}.partition{display:flex;height:24px;gap:2px;margin-top:16px}.partition span{background:#087f6b;border-radius:3px;min-width:1px}.partition span:nth-child(even){background:#6b9c8b}.source-file{background:white;border:1px solid var(--border);border-radius:10px;margin:14px 0;overflow:hidden}.source-file summary{cursor:pointer;padding:17px 21px;font:14px/1.7 Consolas,monospace;overflow-wrap:anywhere}.source-file summary small{color:var(--muted);margin-left:15px}.source-file>p{padding:0 21px;font-size:13px}.source-code{padding:18px 12px;margin:0;background:#13272d;color:#d8e8e2;overflow:auto;font-size:12px;line-height:1.7;tab-size:4}.source-line{display:block;min-height:1.7em;scroll-margin-top:80px}.source-line:target{background:#5a5736;outline:1px solid #ba9d42}.line-no{color:#76958e;display:inline-block;margin-right:16px;user-select:none}.source-code code{font-size:inherit}.source-code .source-line{white-space:pre}.sources-links li{margin:6px 0}footer{font-size:13px;color:var(--muted);padding:30px 0;border-top:1px solid var(--border);margin-top:50px}.status{position:fixed;right:24px;bottom:20px;background:var(--nav);color:white;border-radius:8px;padding:11px 18px;z-index:40;font-size:14px}.empty{padding:10px}.progress-line{height:3px;position:fixed;top:0;left:258px;right:0;background:#dce7e2;z-index:15}.progress-line span{height:100%;display:block;background:var(--accent);width:0}button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible,summary:focus-visible{outline:3px solid #e0a952;outline-offset:3px}
@media(max-width:1100px){.sidebar{width:220px;padding:25px 17px}main{margin-left:220px;padding:30px 26px}.progress-line{left:220px}.reference-grid,.function-grid{grid-template-columns:1fr}.stats{grid-template-columns:repeat(2,1fr)}.stat:nth-child(2){border-right:0}.stat:nth-child(-n+2){border-bottom:1px solid var(--border)}}
@media(max-width:760px){html{scroll-padding-top:70px}.sidebar{display:none;width:270px;box-shadow:15px 0 60px #0005;padding-top:65px}.sidebar.open{display:block}.mobile-toggle{display:block;position:fixed;top:12px;left:14px;z-index:25;background:var(--nav);color:white;border:1px solid #507f6f;border-radius:7px;padding:7px 13px}.progress-line{left:0}main{margin-left:0;padding:68px 16px 40px}.toolbar{gap:7px;margin-bottom:28px}.toolbar .action{padding:12px 10px;font-size:13px}.search-wrap input{font-size:14px}.hero h1{font-size:30px}.hero .lead{font-size:16px}.panel,.chapter{padding:22px 18px}.section-top{gap:14px}.number{font-size:24px;padding:13px}h2{font-size:25px}.formula-bank,.calculators{grid-template-columns:1fr}.input-grid{gap:10px}.formula{padding:18px}.stat{padding:16px}.code-block pre{font-size:12px}th,td{font-size:13px;padding:10px}.skip{left:70px}.section-heading{margin-top:45px}}
@media print{body{background:white;color:black;font-size:11pt}.sidebar,.toolbar,.mobile-toggle,.progress-line,.copy,.status,.calculators{display:none!important}main{margin:0;max-width:none;padding:0}.panel,.chapter{box-shadow:none;border-color:#bbb;break-inside:auto;margin:12pt 0;padding:14pt}.chapter{break-before:page}.reference-grid,.function-grid,.formula-bank{display:block}.reference,.function,.formula{break-inside:avoid;margin:10pt 0}.source-file{display:none}.formula{background:white}.code-block{background:#f4f4f4;color:#111}.code-label{color:#444}a{color:#222}h2,h3{break-after:avoid}.reference code,.function code{background:none}.stats{break-inside:avoid}}
@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}
'''

CALCULATORS = r'''
<div class="calculators">
<article class="calculator" id="calc-pipeline"><h3>01 / 帧流水线</h3><p>理想两阶段模型，比较稳定吞吐量；不测量本机，也不预测输入延迟。</p><div class="input-grid"><label>CPU 阶段 C（ms）<input id="pipe-cpu" type="number" value="6" min="0.1" max="1000" step="0.1"></label><label>GPU 阶段 G（ms）<input id="pipe-gpu" type="number" value="10" min="0.1" max="1000" step="0.1"></label></div><div class="bar-row"><span>串行</span><div class="bar-track"><span id="bar-serial-cpu" class="bar-cpu"></span><span id="bar-serial-gpu" class="bar-gpu"></span></div></div><div class="bar-row"><span>稳态间隔</span><div class="bar-track"><span id="bar-pipe" class="bar-cpu"></span></div></div><div id="pipeline-result" class="result-box" role="status" aria-live="polite"></div></article>
<article class="calculator" id="calc-dispatch"><h3>05 / 工作组与数据量</h3><p>计算派发组数、越界线程与粒子缓冲。工程当前组大小固定为 256。</p><div class="input-grid"><label>粒子数量 N<input id="dispatch-count" type="number" value="10003" min="1" max="1048576" step="1"></label><label>每组线程 B<select id="dispatch-group"><option>64</option><option>128</option><option selected>256</option><option>512</option><option>1024</option></select></label></div><div id="dispatch-result" class="result-box" role="status" aria-live="polite"></div><p>在此修改 B 只是数学实验。真正修改工程时须同时改 HLSL numthreads 和 C++ 派发公式。</p></article>
<article class="calculator" id="calc-partition"><h3>04 / CPU 任务分区</h3><p>观察不能整除时的左右边界，所有区间均为左闭右开。</p><div class="input-grid"><label>元素数量 N<input id="partition-count" type="number" value="10003" min="1" max="1048576" step="1"></label><label>工作线程 W<input id="partition-workers" type="number" value="3" min="1" max="32" step="1"></label></div><div class="partition" id="partition-bars" aria-hidden="true"></div><div id="partition-result" class="result-box" role="status" aria-live="polite"></div></article>
<article class="calculator" id="calc-timestamp"><h3>02 / GPU 时间戳</h3><p>A/B 用整数解析，避免 JavaScript 大整数的低位精度丢失；最后换算毫秒。</p><label>起始 A（tick）<input id="stamp-a" type="text" inputmode="numeric" value="1000000"></label><label>结束 B（tick）<input id="stamp-b" type="text" inputmode="numeric" value="1250000"></label><label>频率 F（tick/s）<input id="stamp-f" type="text" inputmode="numeric" value="1000000000"></label><label><input id="stamp-disjoint" type="checkbox"> Disjoint = true（本次计时无效）</label><div id="timestamp-result" class="result-box" role="status" aria-live="polite"></div></article>
<article class="calculator" id="calc-amdahl"><h3>04 / 串行部分的限制</h3><p>Amdahl 理想模型忽略调度与带宽开销。p 是基线中可并行工作占比。</p><div class="input-grid"><label>可并行比例 p（0–1）<input id="amdahl-p" type="number" value="0.75" min="0" max="1" step="0.01"></label><label>并行度 W<input id="amdahl-w" type="number" value="4" min="1" max="1024" step="1"></label></div><div id="amdahl-result" class="result-box" role="status" aria-live="polite"></div></article>
</div>'''

SCRIPT = r'''
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const fmt = (n,d=3) => n.toLocaleString('zh-CN',{maximumFractionDigits:d});
  function value(id,min,max,integer=false){const el=$(id),raw=el.value.trim(),v=Number(raw);if(!raw||!Number.isFinite(v)||v<min||v>max||(integer&&!Number.isInteger(v)))throw new Error(`请输入 ${min}–${max} 范围内的${integer?'整数':'有效数值'}。`);return v;}
  function result(id,fn){const el=$(id);try{el.textContent=fn();el.classList.remove('error');}catch(e){el.textContent=e.message;el.classList.add('error');}}
  function pipeline(){result('pipeline-result',()=>{const c=value('pipe-cpu',.1,1000),g=value('pipe-gpu',.1,1000),sum=c+g,period=Math.max(c,g);$('bar-serial-cpu').style.width=`${100*c/sum}%`;$('bar-serial-gpu').style.width=`${100*g/sum}%`;$('bar-pipe').style.width=`${100*period/sum}%`;return `串行：${fmt(sum)} ms / ${fmt(1000/sum,2)} FPS\n理想稳态：${fmt(period)} ms / ${fmt(1000/period,2)} FPS\n理想吞吐量提升：${fmt(sum/period)} 倍`;});}
  function dispatch(){result('dispatch-result',()=>{const n=value('dispatch-count',1,1048576,true),b=value('dispatch-group',1,1024,true),g=Math.ceil(n/b);return `工作组：${fmt(g)}\n逻辑线程：${fmt(g*b)}，其中 ${fmt(g*b-n)} 个越界返回\n粒子数据：${fmt(16*n)} B / ${fmt(16*n/1048576,6)} MiB\n4 槽 staging 数据：${fmt(64*n/1048576,6)} MiB`;});}
  function partition(){ $('partition-bars').replaceChildren();result('partition-result',()=>{const n=value('partition-count',1,1048576,true),w=value('partition-workers',1,32,true),lines=[];for(let i=0;i<w;i++){const first=Math.floor(n*i/w),last=Math.floor(n*(i+1)/w),bar=document.createElement('span');bar.style.flex=String(last-first);bar.title=`worker ${i}: [${first}, ${last})`;if(last>first)$('partition-bars').append(bar);lines.push(`worker ${i}：［${first}, ${last}）→ ${last-first} 个`);}return lines.join('\n');});}
  function timestamp(){result('timestamp-result',()=>{if($('stamp-disjoint').checked)throw new Error('计时无效：Disjoint = true，不能解释本次时间戳差值。');const read=id=>{const s=$(id).value.trim();if(!/^\d+$/.test(s))throw new Error('时间戳和频率必须是非负十进制整数。');const n=BigInt(s);if(n>18446744073709551615n)throw new Error('数值超过 uint64_t 范围。');return n;};const a=read('stamp-a'),b=read('stamp-b'),f=read('stamp-f');if(b<a)throw new Error('本计算器要求 B ≥ A。请检查源帧、计时区间或计数器回绕。');if(f===0n)throw new Error('Frequency 必须大于 0。');const ms=1000*Number(b-a)/Number(f);return `差值：${(b-a).toString()} tick\nGPU 时间区间：${fmt(ms,9)} ms\n换算采用近似浮点显示，整数相减保持精确。`;});}
  function amdahl(){result('amdahl-result',()=>{const p=value('amdahl-p',0,1),w=value('amdahl-w',1,1024,true);return `理想加速比：${fmt(1/((1-p)+p/w))} 倍\n无限并行度上限：${p===1?'模型中无有限上限':fmt(1/(1-p))+' 倍'}\n真实任务还需支付派发、汇合、缓存与带宽成本。`;});}
  const groups=[['calc-pipeline',pipeline],['calc-dispatch',dispatch],['calc-partition',partition],['calc-timestamp',timestamp],['calc-amdahl',amdahl]];
  for(const [id,fn] of groups){$(id).addEventListener('input',fn);$(id).addEventListener('change',fn);fn();}
  let toastTimer;function toast(s){$('status').textContent=s;$('status').hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('status').hidden=true,2600);}
  document.querySelectorAll('.copy').forEach(button=>button.addEventListener('click',async()=>{const text=button.parentElement.querySelector('pre').innerText;try{if(!navigator.clipboard)throw new Error('fallback');await navigator.clipboard.writeText(text);toast('已复制。请在项目根目录执行。');}catch{const area=document.createElement('textarea');area.value=text;area.style.position='fixed';area.style.opacity='0';document.body.append(area);area.select();const ok=document.execCommand('copy');area.remove();toast(ok?'已复制。请在项目根目录执行。':'请手动选中代码后复制。');}}));
  $('print').addEventListener('click',()=>window.print());
  $('menu-toggle').addEventListener('click',()=>{const open=$('sidebar').classList.toggle('open');$('menu-toggle').setAttribute('aria-expanded',String(open));});
  function revealHash(){if(!location.hash)return;let id;try{id=decodeURIComponent(location.hash.slice(1));}catch{return;}const el=$(id);if(!el)return;let parent=el.parentElement;while(parent){if(parent.tagName==='DETAILS')parent.open=true;parent=parent.parentElement;}requestAnimationFrame(()=>el.scrollIntoView({block:'start'}));}
  window.addEventListener('hashchange',revealHash);revealHash();
  document.querySelectorAll('a[href^="#"]').forEach(a=>a.addEventListener('click',()=>{if(a.closest('nav')){$('sidebar').classList.remove('open');$('menu-toggle').setAttribute('aria-expanded','false');}if(a.hash===location.hash)revealHash();}));
  const entries=[...document.querySelectorAll('.searchable')].map(el=>({id:el.id,title:el.dataset.title,text:el.textContent.toLowerCase()}));
  $('search').addEventListener('input',()=>{const q=$('search').value.trim().toLowerCase();$('search-results').replaceChildren();if(!q){$('search-results').hidden=true;return;}const terms=q.split(/\s+/),hits=entries.filter(e=>terms.every(t=>e.text.includes(t)||e.title.toLowerCase().includes(t)));$('search-results').hidden=false;const count=document.createElement('p');count.textContent=hits.length?`找到 ${hits.length} 项，显示前 12 项`:'没有匹配项。试试 count、Map、帧龄或 Dispatch。';$('search-results').append(count);for(const h of hits.slice(0,12)){const a=document.createElement('a');a.href='#'+h.id;a.textContent=h.title;a.addEventListener('click',()=>{$('search-results').hidden=true;$('search').value='';if(a.hash===location.hash)revealHash();});$('search-results').append(a);}});
  document.addEventListener('keydown',e=>{if(e.key==='Escape'){$('search-results').hidden=true;$('sidebar').classList.remove('open');$('menu-toggle').setAttribute('aria-expanded','false');}});
  document.addEventListener('click',e=>{if(!e.target.closest('.search-wrap'))$('search-results').hidden=true;});
  const nav=[...document.querySelectorAll('nav a')];const sectionTargets=nav.map(a=>document.querySelector(a.getAttribute('href'))).filter(Boolean);
  function progress(){const total=document.documentElement.scrollHeight-innerHeight;$('progress').style.width=`${total>0?100*scrollY/total:0}%`;let current='start';for(const s of sectionTargets){if(s.getBoundingClientRect().top<160)current=s.id;}for(const a of nav){const active=a.hash==='#'+current;a.classList.toggle('active',active);if(active)a.setAttribute('aria-current','location');else a.removeAttribute('aria-current');}}
  window.addEventListener('scroll',progress,{passive:true});window.addEventListener('resize',progress);progress();
})();
'''

nav=''.join(f'<a href="#ch{c[0]}">{c[0]} · {esc(c[2])}</a>' for c in chapters)
formula_cards=''.join(f'<article class="formula searchable" id="formula-{i}" data-title="{esc(title)}"><h3>{esc(title)}</h3><p>{esc(eq)}</p><small>{esc(desc)}</small><small><strong>算例：</strong>{esc(example)}</small></article>' for i,(title,eq,desc,example) in enumerate(formulas))

html=f'''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light"><meta name="description" content="与 C++/HLSL 教学工程对应的 DX11 CPU/GPU 并行手册：8 章、变量字典、函数地图、公式推导与交互计算。"><title>DX11 并行编程 · 教学手册</title><style>{STYLE}</style></head>
<body><a class="skip" href="#start">跳到正文</a><div class="progress-line" aria-hidden="true"><span id="progress"></span></div><button id="menu-toggle" class="mobile-toggle" aria-controls="sidebar" aria-expanded="false">目录</button>
<aside class="sidebar" id="sidebar"><a href="#start" class="brand"><span>11</span>并行实验室</a><p class="side-caption">DIRECT3D · C++ · HLSL</p><nav aria-label="学习目录"><a href="#start" class="active">开始 · 阅读地图</a><div class="nav-group">八章实验</div>{nav}<div class="nav-group">随查随用</div><a href="#variables">变量与内存布局</a><a href="#functions">函数与调用关系</a><a href="#formulas">公式与推导</a><a href="#lab">交互计算实验室</a><a href="#sources">源码对照</a><a href="#references">参考与运行说明</a></nav><div class="side-foot">单文件 · 可离线阅读<br>与本工程实际实现对照<br><a href="../README.md">返回工程 README ↗</a></div></aside>
<main><div class="toolbar"><div class="search-wrap"><label class="visually-hidden" for="search" style="position:absolute;width:1px;height:1px;overflow:hidden;clip-path:inset(50%)">搜索章节、变量、函数与公式</label><input id="search" type="search" placeholder="搜索变量、函数、公式，例如 Dispatch 或帧龄" autocomplete="off" aria-controls="search-results"><div id="search-results" class="results" hidden></div></div><button id="print" class="action">打印 / PDF</button></div>
<header class="hero" id="start"><p class="eyebrow">A PRACTICAL GUIDE / DX11 CLASSROOM</p><h1>从一帧出发，<br>读懂 CPU 与 GPU 并行。</h1><p class="lead">把变量、函数和公式放回执行流程里。先知道数据由谁拥有、什么时候可用，再用本地教学工程验证每一次“更快”。</p><div class="hero-tags"><span class="tag">C++17 / Feature Level 11_0</span><span class="tag">HLSL Shader Model 5.0</span><span class="tag">无需联网或启动服务</span></div><div class="stats"><div class="stat"><b>08</b><span>章节与对照实验</span></div><div class="stat"><b>{len(variables):02}</b><span>变量 / 状态条目</span></div><div class="stat"><b>{len(functions):02}</b><span>函数 / 调用条目</span></div><div class="stat"><b>{len(formulas):02}</b><span>公式与适用条件</span></div></div></header>
<section class="panel"><p class="eyebrow">HOW TO READ</p><h2>先找到数据，再追踪调用。</h2><p>每章依次说明“教什么 → 变量 → 调用路径 → 公式 → 动手实验”。遇到陌生符号，搜索变量或函数；点击源码链接可在当前页面展开真实代码。所有计算器只是教学数学模型，不会执行 DX11 或测量你的显卡。</p><div class="call-map" aria-label="主循环调用关系"><span>main → Run</span><i>→</i><span>Device::Init<br>Lesson::Init</span><i>→</i><span>Poll → Begin<br>Lesson::Frame → End</span><i>→</i><span>Present</span><i>→</i><span>下一帧 / 退出验证</span></div><p class="note"><strong>三个易混点：</strong>ComputeParticles::Step 是 CPU 函数，UpdateCS 才在 GPU 执行；清空 UAV 绑定不会等待 GPU；Options::cpuMs 是模拟负载，Metrics::cpuMs 是测量结果。</p></section>
{render_foundations(esc, table)}
{render_chapters()}
<div class="section-heading" id="variables"><p class="eyebrow">REFERENCE / VARIABLES</p><h2>变量与内存布局</h2><p class="lead">名字相似不代表语义相同。关注类型、单位、所有者和有效期。</p></div>
<section class="panel"><h3 style="margin-top:0">两个 16 字节结构，承担不同职责</h3>{table(['结构','字节 0–3','字节 4–7','字节 8–11','字节 12–15'],[('Particle','x','y','vx','vy'),('Constants / Parameters','count / particleCount','dt / deltaTime','time / elapsedTime（未使用）','grid / gridSize（未使用）')])}<p>Particle 的结构化缓冲 stride 为 16 B；Constants 使用 alignas(16)，四个 32-bit 字段共 16 B。<code>pData + destination</code> 若已转为 Particle*，偏移单位就是粒子，而不是字节。两个未使用的常量字段仍保留在布局中，当前不影响结果。</p></section><div class="reference-grid">{render_variables()}</div>
<div class="section-heading" id="functions"><p class="eyebrow">REFERENCE / FUNCTIONS</p><h2>函数与调用关系</h2><p class="lead">输入输出、执行位置和等待行为，比记住函数名字更重要。</p></div><div class="function-grid">{render_functions()}</div>
<div class="section-heading" id="formulas"><p class="eyebrow">MATH / MEANING BEFORE NUMBERS</p><h2>公式、单位与适用条件</h2><p class="lead">先写出前提，再代入数值。模型给出解释框架，最终结论仍要看正确性和测量。</p></div><div class="formula-bank">{formula_cards}</div>
<div class="section-heading" id="lab"><p class="eyebrow">TRY IT / CHANGE ONE VARIABLE</p><h2>交互计算实验室</h2><p class="lead">改变参数，检查直觉。结果在本页即时计算，输入错误会明确显示。</p></div>{CALCULATORS}
<div class="section-heading" id="sources"><p class="eyebrow">SOURCE / FOLLOW THE IMPLEMENTATION</p><h2>源码对照</h2><p class="lead">包含公共框架、HLSL 和八章入口的静态快照。原文件修改后，在工程根目录执行 <code>python scripts/generate_handbook.py</code> 更新本页。</p></div>{render_sources()}
<section class="panel" id="references"><p class="eyebrow">NEXT / RUN THE EXPERIMENT</p><h2>把推导交给实验验证</h2>{block('.\\scripts\\setup.ps1 -CheckOnly\n.\\scripts\\build.ps1\n.\\scripts\\run.ps1 -Chapter 1','PowerShell · 项目根目录')}<p>正式性能采样用 Release、硬件 GPU、固定参数，先做带 Debug Layer 的正确性验证，再移除 debug 比较耗时。不同模式单独启动，报告有效 GPU 样本和丢样；<code>−1</code> 是缺失状态，不是零成本。</p><p><a href="measurement.md">测量字段与实验记录</a> · <a href="verification.md">工程验证记录</a> · <a href="../README.md">完整参数和目录</a></p><ul class="sources-links"><li><a href="https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render">Microsoft · Immediate and Deferred Rendering</a></li><li><a href="https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources">Microsoft · Dynamic resources</a></li><li><a href="https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader">Microsoft · Compute Shader Overview</a></li><li><a href="https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata">Microsoft · GetData</a></li><li><a href="https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint">Microsoft · Timestamp Disjoint</a></li></ul></section>
<footer>DX11 并行教学工程 · 单文件 HTML 手册 · 生成器仅依赖 Python 标准库。阅读无需 Python。页面内源码是生成时快照，原文件入口使用相对路径。打印时保留教学内容、变量、函数与公式，省略交互计算器和完整源码。</footer></main><div id="status" class="status" role="status" aria-live="polite" hidden></div><noscript><p style="padding:20px">JavaScript 未启用：正文、变量、函数和公式仍可阅读；搜索、计算器和源码自动展开需要启用 JavaScript。</p></noscript><script>{SCRIPT}</script></body></html>'''
OUT.write_text(html, encoding='utf-8')
print(f'Generated {OUT} ({len(html.encode("utf-8")):,} bytes): {len(chapters)} chapters, {len(variables)} variable entries, {len(functions)} function entries, {len(formulas)} formulas.')
