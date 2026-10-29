"""Beginner walkthroughs; rendered into the standalone handbook by the generator."""

FOUNDATIONS = [
('先运行，再找入口', '双击根目录 Start-Learning.cmd，输入章节数字。先观察窗口，再按 M 比较模式。窗口左侧数字框可以直接修改参数，下拉框选择模式；停顿 0.6 秒自动应用。需要导出固定参数的对照数据时，再使用每章折叠区中的采样命令。用 Visual Studio 打开 build/DX11ParallelClassroom.slnx 后，选择 Release / x64；从 src/chapters/对应章节.cpp 的 main 开始。每个可执行文件只选择一个 Lesson，但复用 common 里的窗口、设备、计时和绘制代码。'),
('main、Init、Frame、Validate 分别何时运行？', 'main 把章节对象交给 Run。Run 创建设备，再调用章节 Init 建立资源。之后每帧调用 Frame：更新数据、发出绘制命令；框架随后 Present。退出时按参数进行验证。Init 是准备材料，Frame 是重复执行的工作。不要把创建缓冲、编译着色器、创建线程放进每帧流程。按 M 切换模式会重新初始化章节状态，因此不应把切换前后的粒子当作同一条连续模拟轨迹。'),
('读懂 Frame 的四个参数', 'Device &gpu 是对设备包装对象的引用，里面 device 负责创建资源，context 负责设置状态和发命令。const Options &o 是只读运行配置，例如 o.count；Metrics &m 是本帧输出记录，函数可写 m.updateMs；float dt 是模拟时间步长，单位秒。这里 dt 固定为 1/60，不是上一帧真实耗时：即使窗口只有 30 FPS，每步仍只模拟 1/60 秒。'),
('C++ 符号不需要一次背完', '对象.成员 用于普通对象；指针->成员 用于指针。std::vector<Particle> 是连续粒子数组，size() 是元素数，data() 是首元素地址。auto 让编译器推导类型，不是运行时变类型。成员名结尾的下划线只是本工程命名约定。override 表示实现 Lesson 定义的虚函数；Run 调用 Frame 时实际进入当前章节的实现。std::make_unique 创建由智能指针管理的对象。'),
('ComPtr、Get 与输出地址', 'ComPtr<ID3D11Buffer> 管理一个 D3D 资源接口的引用生命周期。buffer.Get() 取接口指针，交给 API 使用，并不复制整块 GPU 数据。ReleaseAndGetAddressOf() 先释放旧接口，再给创建函数一个可以写入新接口指针的位置。看到 Check(...) 时先读里面的 API：Check 只是检查 HRESULT，失败时抛异常；它不会让异步 GPU 命令自动执行完成。'),
('数据、资源、视图是三件事', 'CPU vector 是 CPU 能直接访问的数据。ID3D11Buffer 是 D3D 资源，不能把它的接口指针当成粒子地址。SRV 是供着色器读取资源的视图；UAV 是供计算着色器写入等用途的视图。同一份粒子 Buffer 可有两种视图，不是凭空复制两份粒子。STAGING 缓冲用于 CPU 回读，DYNAMIC 缓冲用于本工程的 CPU 写入；资源创建时的用途决定后续允许的访问方式。'),
('一颗粒子怎样变成屏幕上的点？', 'CPU 的 Particle 是 x、y、vx、vy 四个 float，共 16 字节。HLSL 用两个 float2 表示同样布局。每步 x += vx × dt，y 同理；越过边缘后回绕。绘制时 CpuVS 从顶点缓冲取位置，GpuVS 按 SV_VertexID 从 SRV 取位置，二者都调用 MakeParticle，把 [0,1) 的坐标映射到约 [-0.95,0.95) 的裁剪空间。PS 输出颜色。速度不直接决定颜色，也不是屏幕像素坐标。'),
('CPU 调用顺序与 GPU 执行顺序', '调用 Draw/Dispatch 的 C++ 代码由 CPU 执行；它安排 GPU 工作。HLSL 的 VS、PS、UpdateCS 才在 GPU 上运行。在本工程同一 Immediate Context 命令流中，先 Dispatch 再 Draw 可以表达更新后绘制的依赖，不必在两者之间让 CPU 等待。CPU/GPU 可以重叠，不等于两个 GPU 阶段一定同时执行。Flush 推动提交，但不等于完成；WaitIdle 才是本工程显式等待完成的工具。'),
('怎样读一次测量', '先保证两个模式的 count、draws、frames 等输入相同，再比较 CSV 和同名 JSON 汇总。updateMs 是章节自己圈定的 CPU 区间，各章含义不同；gpu_interval_ms 是延后取得的 GPU 时间戳区间；presentMs 单独统计呈现调用。负值表示没有样本，不要计成零。先用 --validate 检查结果，再用不带 --debug 的 Release 做性能比较。一次跑得更快不能证明普遍更快，要重复运行并记录硬件、模式和参数。'),
]

# scenario, ownership, ordered annotations, worked example, experiment, pitfalls, completion
LESSONS = {
'01': dict(
 scenario='想象游戏每帧先计算角色逻辑，再把画面任务交给显卡。本章用忙循环代替角色逻辑，用重复绘制代替可调的图形负载。窗口里的粒子保持静止是正常的：本章没有调用 Step 更新粒子。你要观察的是等待怎样改变帧耗时，而不是动画。',
 ownership='初始化时粒子上传到 GPU，后续重复读取它来画点。CPU 上的 work 只是忙循环中的整数，不是粒子坐标。整个实验不需要 CPU 工作线程，CPU 与 GPU 已经是两个可以独立推进的处理器。',
 steps=[('start 与 while：制造 CPU 工作', 'Clock::now() 记下开始时刻，Ms(start) 返回已经过去的毫秒。o.cpuMs=4 时循环约忙碌 4 ms；这不是 Sleep，CPU 一直在算整数。volatile 让这段访问不能简单当作无用计算删掉，但它不提供线程同步。m.updateMs 只记录这一段。'),('for：安排 GPU 绘制', 'o.gpuRepeats=32 就调用 32 次 renderer_.Gpu，每次画 o.count 个点。重复画相同位置，视觉上不一定变复杂，工作量却增加。调用返回表示 CPU 已完成这次调用，不表示显卡已经画完。次数越多也会增加 CPU 提交成本，因此不能把它当作纯 GPU 延时旋钮。'),('if：唯一的等待差别', 'mode 0 跳过 WaitIdle，CPU 可以继续走向下一帧；mode 1 调用 WaitIdle，等待前面安排的 GPU 工作结束再继续。这会阻止 CPU 提前准备下一帧。Present 由外部 Run 调用，不在这里的 Frame 内。')],
 example='假设 CPU 准备恰好 6 ms、GPU 执行恰好 10 ms。第一帧从准备到执行完仍要约 16 ms。若每帧都等，第二帧只能在 16 ms 后开始；若不等，CPU 可以在 GPU 处理第一帧时准备第二帧。理想稳定输出间隔受较慢阶段限制，约 10 ms。这里忽略提交、显示和缓冲上限，只用于理解吞吐量；不是对本机帧率的承诺。',
 experiment='先用 CPU 负载 4 GPU 重复次数 32。比较两次报告的 CPU 帧耗时分位数和 GPU 区间。再只把 cpu-ms 改为 0：如果本来主要受 CPU 限制，现象会变化。接着恢复 4，只提高 gpu-repeat。预期每帧等待会减少跨帧重叠；若两种模式相近，检查负载是否太轻、是否开着垂直同步、是否还有其他瓶颈。',
 pitfalls='不要为了“保证正确”在每个 Draw 后加 WaitIdle；本章静态数据无需这样同步。也不要把更高 FPS 解读成输入延迟必然降低：排队可能增加延迟。游戏中真正依赖 GPU 结果的操作，需要明确的完成条件，而不是全面取消同步。',
 completion='能在 Frame 里指出 CPU 忙循环、GPU 工作的提交位置、强制等待位置，并解释为何单个 CPU 渲染线程也能与 GPU 重叠。'),
'02': dict(
 scenario='看到 Draw 调用只花 0.1 ms，能不能说 GPU 只用了 0.1 ms？本章专门解决这个测量误区。画面仍是静态点阵；学习目标是区分 CPU 发命令的成本与 GPU 时间线上的区间。',
 ownership='章节 Frame 只记录 CPU Draw 调用时间。GPU 计时器在公共框架中，拥有 16 个槽，每槽存起止时间戳、disjoint 查询、源帧号与 pending 状态。Query 存的是 GPU 可稍后提供的结果，不是调用后立刻得到的普通整数。',
 steps=[('先看章节：updateMs 包围了什么', 'start 在循环之前，m.updateMs 在循环之后。因此本章 updateMs 包含多次 renderer_.Gpu 的 CPU 调用成本。后面的 WaitIdle 不在 updateMs 中，但仍在外层 CPU 帧计时中。若只盯 updateMs，可能完全漏掉等待带来的成本。'),('再看 Run：先查旧结果，再记录本帧', '每帧 Poll 检查已有 Query；Begin/End 在 GPU 命令流中包围 Lesson::Frame。GPU 时间戳使用 End(query) 写标记，名字不是“结束整个程序”。disjoint 查询则用 Begin/End 包围区间，提供频率及有效性。'),('最后看 GpuTimer::Poll：结果可晚到', 'GetData 返回 S_FALSE 表示尚未准备好，保留 pending，下一轮再试。结果齐全、Disjoint=false、Frequency>0 才换算毫秒。结果写回 Slot::frame 对应的旧帧；16 槽都在用时跳过本帧采样，不强行等待腾位置。')],
 example='假设第 20 帧起点 A=1,000,000，终点 B=1,250,000，频率 F=1,000,000,000 tick/s。差值 250,000 tick 除以频率是 0.00025 秒，再乘 1000 得 0.25 ms。即使直到第 23 帧才拿到结果，它仍属于第 20 帧；不能与第 23 帧 CPU 数据直接配对。',
 experiment='固定 GPU 重复次数 32，分别运行 mode 0/1。检查 CSV 的 update_ms、gpu_interval_ms 和 CPU 帧耗时。再提高 repeat，观察两个时钟的数值都可能增长，但增长幅度不同。留意有效 GPU 样本数；空缺不是瞬间完成。窗口显示最近 10 秒的均值，同时显示 CPU/GPU 有效样本数；GPU 结果按源帧归入窗口，晚到且已过期的结果不纳入当前均值。',
 pitfalls='本工程的 GPU 区间不包含开头清屏和最后交换链复制/Present；它还可能包含 GPU 等待后续命令到达的空隙，因此不是“纯运算单元忙碌时间”。不要拿 CPU Clock 的 tick 与 GPU timestamp 相减；频率和时钟域不同。',
 completion='能说明每个指标实际包围哪段代码，并把晚到的 GPU 结果归到正确源帧。'),
'03': dict(
 scenario='CPU 每帧算完粒子位置，需要把结果交给 GPU。若 CPU 覆盖了 GPU 仍在读取的内存，就出现访问冲突。本章比较两种合法上传方式；两种都按 1024 个粒子一批绘制，画面应相同。',
 ownership='data_ 是 CPU 最新粒子数组；renderer_ 内部有可由 CPU 写入的 DYNAMIC 顶点缓冲。Map 成功后才获得 mapped.pData，Unmap 后不能继续写它。GPU 最终读取顶点缓冲，不会直接读取 std::vector。',
 steps=[('先更新 CPU 数组', 'UpdateParticles(data_, 0, data_.size(), dt) 遍历全部粒子。0 是起始元素下标，size() 是不包含的结束边界；这一步不调用着色器。m.updateMs 只记录模拟更新。'),('两个 bool 决定上传策略', 'renderer_.Cpu 的第四个参数 o.mode==1 表示是否使用向后追加的 stream 模式，第五个 true 表示分批。进入 ParticleRenderer::Cpu 后，first 每批递增 1024，n 取当前批实际数量，防止最后一批越界。'),('Map → memcpy → Unmap → Draw', 'mode 0 每批 DISCARD，destination 都是 0：旧逻辑内容作废，运行时可以另给存储以保留 GPU 需要的旧版本。mode 1 首批 DISCARD，后续 NO_OVERWRITE，destination=first：后续只写没用过的区间。Draw(n,destination) 从本批写入的位置取 n 个顶点。')],
 example='取 count=2500，三批大小是 1024、1024、452。mode 1 写入区间 [0,1024)、[1024,2048)、[2048,2500)，对应 Draw 的起点 0、1024、2048。mode 0 三次都从缓冲开头写并从 0 画，但每次 DISCARD 允许运行时更换底层存储。总上传量仍为 2500×16=40,000 字节，总 Draw 数都是 3。指针已转为 Particle*，加 1024 实际跨过 16,384 字节。',
 experiment='先把 count 改成 2500，确认 drawCalls=3，再改为 10003，预期为 ceil(10003/1024)=10。比较 upload_ms 与整体 CPU 帧耗时；两种路径 Draw 数必须相同。随后增加 count 看成本变化。不要改成一种模式只画一次、另一种画十次，否则无法把差异归因于 Map 策略。',
 pitfalls='NO_OVERWRITE 是你承诺不覆盖在用数据，API 不会替你自动分配空区间。DISCARD 作废的是旧逻辑内容，不保证给出的内存清零；只画已经写过的部分。DISCARD 也不是承诺永远不阻塞，资源重命名和内存压力仍可能产生代价。',
 completion='给定任意 count，能算出每批的 first、n、destination，并说明为何两种方式都不会故意覆盖 GPU 正在读取的逻辑版本。'),
'04': dict(
 scenario='粒子之间互不影响，每颗粒子都只读写自己。因此可以把数组分段交给多个 CPU 线程。本章先学习 CPU 内部的并行，再考虑它与 GPU 的重叠；画面和上一章一样运动。',
 ownership='所有工作线程共享 data_，但写入范围互不重叠。jobs_ 在 Init 创建持久线程，不是每帧创建线程。只有主线程在汇合后调用 renderer_.Cpu 上传数据；工作线程不碰 Immediate Context。',
 steps=[('mode 0 是正确性基准', 'UpdateParticles 一次处理 [0,N)。它的数学规则与并行路径完全相同。并行优化首先要保证总共仍更新 N 个元素，而且每个恰好一次。'),('理解 lambda 与分区', '[&](uint32_t worker) 是一段可调用的匿名函数，[&] 让它引用外围变量。worker 是编号 0 到 W-1。first=N×worker/W、last=N×(worker+1)/W；整数除法向下取整。UpdateParticles 只遍历 first≤i<last。线程运行期间不要 resize 数组，否则地址可能变化。'),('Run 的汇合是数据依赖', 'Jobs::Run 唤醒固定工作线程，等待所有任务完成才返回。此后 m.updateMs 包含唤醒、执行与汇合成本；renderer_.Cpu 才能上传完整的新一帧。这里的等待不能简单删掉，否则主线程可能上传部分旧位置、部分新位置，甚至产生数据竞争。')],
 example='N=10、W=3：线程 0 更新 [0,3)，线程 1 更新 [3,6)，线程 2 更新 [6,10)。分别是 3、3、4 个元素，无遗漏无重复。N=2、W=4 时有些区间为空，这是合法的，但调度成本可能超过两次加法所需时间。区间之间也可能共享缓存行，因此“不同元素”不等于不存在缓存争用。',
 experiment='先用较小 count 比较 mode 0/1，再改成较大 count；保持 count 不变，依次设置 workers=1、2、4、8。记录 update_ms 和总 CPU 帧耗时，不只记录最快一次。预计小任务可能因线程唤醒和汇合而更慢；较大任务才可能摊薄调度成本，之后又受内存带宽或核心数量限制。',
 pitfalls='W 个线程不保证 W 倍加速。Amdahl 模型中，若 80% 可并行、20% 必须串行，4 线程理想加速也只有 1/(0.2+0.8/4)=2.5 倍，现实还有调度成本。若以后加入粒子间碰撞，当前“每个粒子独立”的前提不再成立，不能原样分区写入。',
 completion='能手算非整除分区，解释为何必须等待 CPU 任务结束再上传，并指出多线程慢于单线程的合理原因。第 04 章现在分别显示 GPU 帧区间与 GPU 绘制区间：后者在 CPU 上传结束后紧贴单次 Draw 前后打点，两者共享时钟有效性检查。前者可能包含供给空隙；后者仍不是纯 GPU 忙碌时间。不能用两项之差当作 CPU 耗时。CSV 的 gpu_draw_ms 保存绘制区间，其他章节未测量时为 -1。'),
'05': dict(
 scenario='现在把位置更新搬到 GPU，并让渲染直接使用更新后的 GPU 数据。每帧不把粒子搬回 CPU，这才是本章的主要价值。两个模式都让点运动，肉眼应看不出算法差异。',
 ownership='mode 0 的最新状态在 particles_ 的 GPU Buffer 中，cpu_ 不随它同步更新。mode 1 的最新状态在 cpu_，每帧上传给顶点缓冲。Init 给两边同样的初始值，Validate 才在退出时把结果与 CPU 参考比较。不能用未更新的 cpu_ 判断 GPU 模式算错。',
 steps=[('Frame 的分支就是两条数据路径', 'mode 0 调用 Step 发出计算工作，再用 particles_.srv 画图；mode 1 调用 CPU UpdateParticles，再 renderer_.Cpu 上传并画图。GPU 模式的 m.updateMs 记录 Step 的 CPU 调用成本，CPU 模式记录实际 CPU 模拟成本，所以不能只比这个字段就宣称 GPU 计算快了多少。'),('Step：设置参数和派发', 'Step 将 count 与 dt 写入常量缓冲，绑定计算着色器和 UAV，再 Dispatch((count+255)/256,1,1)。C++ 的 Step 本身仍运行在 CPU。Dispatch 的参数是组数，不是粒子数；每组的线程数由 HLSL [numthreads(256,1,1)] 决定。'),('UpdateCS：每个线程处理一个元素', 'id.x 是全局线程编号。若 id.x>=particleCount 立刻返回；否则读 writableParticles[id.x] 到局部 p，更新位置并回绕，再写回同一元素。每个线程不访问其他粒子的写入结果，所以这里不需要组共享内存或组内屏障。'),('计算后直接绘制', 'Step 解除 UAV 绑定，renderer_.Gpu 把同一资源作为 SRV 提供给 GpuVS；GpuVS 以顶点编号读取位置。解绑是处理绑定冲突，不是让 CPU 等待 GPU。同一命令流的先计算后绘制表达了依赖，不意味着计算和这次绘制同时执行。')],
 example='count=10003 时，组数=(10003+255)/256 的整数结果为 40，共启动 40×256=10240 个线程。编号 0…10002 有效，剩余 237 个提前返回。若某粒子 x=0.5、vx=0.06、dt=1/60，下一步 x=0.501；若 x=0.9995，则得到 1.0005，减 1 后约 0.0005。此处速度和固定步长保证一步不会跨越多个周期，不能把一次减 1 当作任意大步长的通用取模。',
 experiment='先用 count=10003 和 --validate 验证尾组，再用较大 count 比较两种模式。观察 GPU 区间、CPU 帧耗时和上传成本，而不是仅比较 update_ms。尝试把计算器里的组大小改成 128 只会改变数学演示；真改工程需同时改 HLSL numthreads 和 C++ 派发除数，否则会漏算或过量派发。',
 pitfalls='GPU 驻留适合大量独立视觉粒子；若游戏逻辑每帧立刻需要所有位置，再回读可能吃掉收益。Validate 的 ReadBlocking 是退出时的正确性检查，不代表正常帧每次都回读。Constants 的 time/grid 字段目前没有参与着色器计算，不要据名字推测存在时间动画或网格算法。',
 completion='能沿 count → Dispatch → id.x → 粒子下标追踪一次更新，并说明 Buffer、UAV、SRV 如何指向同一份数据。'),
'06': dict(
 scenario='有时 CPU 确实需要 GPU 结果，比如调试或延迟统计。本章比较“现在立刻要”与“完成后再消费”。两种模式都在 GPU 更新和绘制粒子；回读数据不用于决定本帧点的位置，因此允许使用旧样本。',
 ownership='GPU 粒子缓冲不能直接当 CPU 内存读。先 CopyResource 到 CPU 可读的 STAGING 资源，再 Map。立即模式复用一个 staging_；延后模式有 4 个槽，每槽有 staging、EVENT 查询、源帧号和 pending 状态。槽未完成时不能覆盖。',
 steps=[('每帧先更新 GPU 粒子', 'Step 更新后，后续 CopyResource 才代表这一模拟步的状态。baseFrame_ 记录当前模式开始的当前统计段帧号，steps_++ 的第一次比较用于初始化它；这样切换模式后仍能追踪样本对应的模拟步数。'),('mode 1：请求当前帧，可能等待', 'CopyResource 只是安排复制。紧接着 Map(READ, flags=0) 要求 CPU 马上访问结果，可能阻塞到复制完成。memcpy 将映射内容复制进 result_，Unmap 结束访问。readbackMs 包含复制调用、Map 等待和 CPU 拷贝；readbackAge=0 表示本帧结果。'),('mode 0：先收旧件，再提交新件', 'Poll 检查旧槽 EVENT；完成后才尝试非阻塞 Map，并处理仍在绘制的情况。Enqueue 找空槽，录制 CopyResource 和后续 EVENT，保存当前源帧号。若四槽都 pending，跳过这个采样并增加丢弃计数，让渲染继续。'),('最终验证需要历史参考', 'ring_.latest 不一定是最后一帧。Validate 按 latestFrame-baseFrame+1 个模拟步生成参考，再比较旧样本。若拿旧样本与最终位置比较，差异是时间不同，不一定是 GPU 算错。')],
 example='假设第 10 帧复制进槽 A，第 11 帧查询仍未完成，第 12 帧收到 A：帧龄为 12−10=2。它描述第 10 帧更新后的粒子；不能标成第 12 帧数据。即使已经过两帧也必须看 EVENT，因为 GPU 可能落后更多。四槽全满时，本工程选择少拿一个样本，而不是覆盖最旧但仍在复制的槽。',
 experiment='用相同 count 比较 readback_ms、readback_age、丢弃计数与总帧耗时。mode 1 帧龄应为 0，但可能更耗时；mode 0 允许旧样本和跳过采样。若当前帧没有新回读，相关字段可能为缺失状态。提高 count 后观察复制量增加的影响，不要把较少采样的模式当成每帧全量数据交付。',
 pitfalls='延后回读减少立即等待的机会，但 GPU 到 CPU 的传输与 memcpy 成本仍存在。4 槽是容量选择，不是“4 帧后必定完成”的保证。若碰撞判定必须使用本帧精确结果，本章允许延迟、允许丢样的策略不能直接套用，应先重新设计数据依赖和更新位置。',
 completion='能画出一个槽从空闲、待完成、可读取到重新空闲的过程，并解释帧龄与正确性的关系。'),
'07': dict(
 scenario='场景里有很多小物体时，CPU 发出大量 Draw 本身可能成为瓶颈。本章用网格三角形制造许多 Draw，尝试让多个 CPU 线程同时录制命令。画面从粒子变成三角形网格；这不是 GPU 计算着色器实验。',
 ownership='每个工作线程独占一个 Deferred Context 和一个 Command List 槽位。静态网格顶点缓冲供它们读取。主线程独占 Immediate Context，最后依次回放各命令列表。每个线程有自己的录制状态，不共享一个可随意并发调用的 context。',
 steps=[('Init 建立固定资源', 'InitGrid 为 o.draws 个三角形准备 3×draws 个顶点。CreateDeferredContext 为每个 worker 创建录制上下文。CheckFeatureSupport 打印驱动线程支持信息；有接口可用不等于这台机器一定能获得性能收益。'),('每个 worker 录制自己的片段', 'ClearState 清理本录制上下文状态，BindTarget 绑定目标，GridState 绑定着色器、顶点布局、缓冲和其他所需状态。然后按 Draw 数分区。Draw(3,i*3) 每次读 3 个顶点，起点递增，画不同三角形。Deferred 上这些调用在记录工作，不是直接让每个 worker 启动一块 GPU。'),('FinishCommandList 封装录制结果', '每个 worker 把命令封装进 lists_[worker]。FALSE 表示不请求保留录制前的状态；下次录制前重新完整绑定。Jobs::Run 汇合之后，主线程才可以安全读取各列表。m.updateMs 到此为止，因此不包含下面的回放循环。'),('ExecuteCommandList 在主线程依次回放', 'for(auto &list:lists_) 把各列表放入 Immediate 命令流。这里 CPU 录制可以并行，最终回放仍顺序进行。FALSE 不要求保存并恢复回放前的 Immediate 状态；后续需要的状态应显式绑定。')],
 example='draws=10、workers=3 时，分工是 Draw 编号 [0,3)、[3,6)、[6,10)。线程 1 的第一条是 Draw(3,9)，读顶点 9、10、11；最后一条 Draw(3,15) 读 15、16、17。总计仍是 10 次 Draw、30 个顶点。并行改变的是谁录制，不是把三角形数量变少。',
 experiment='先用 draws=64，再用 4096 或更大，分别比较 mode 0/1；固定 Draw 数后改变 workers。主要看总 CPU 帧耗时，因为 deferred 路径的 update_ms 不包含回放；不要把它直接当成完整渲染 CPU 成本。小场景常被唤醒、状态绑定、封装、回放开销抵消；本实验不承诺多线程必定快。',
 pitfalls='不要让所有线程共用 Immediate Context，也不要认为一个 Command List 对应一个硬件队列。若只是减少大量小 Draw，实例化或批处理也值得评估，但本章刻意保持 Draw 数相同以研究录制成本。Debug Layer 检查可帮助发现漏绑定状态；性能比较时再关闭它。',
 completion='能解释“录制并行、回放顺序”的边界，指出每个线程独占什么，并追踪某条 Draw 对应的顶点范围。'),
'08': dict(
 scenario='把之前的模块组合成一个小型游戏帧：CPU 更新游戏对象 actors_，GPU 更新视觉粒子 particles_，CPU 偶尔消费 GPU 样本。重点是安排独立工作与等待位置，不是创造一种新的更新公式。两份点可能重叠，因为它们初始值和运动规则相同。',
 ownership='actors_ 的权威状态在 CPU，particles_ 的权威状态在 GPU，二者在本实验没有本帧相互读取的依赖。因此 GPU 粒子工作可以先安排，再让 CPU 线程更新 actors_。readback_ 或 ring_.latest 只是观察 GPU 的样本，不反馈到 CPU 对象更新。',
 steps=[('mode 0 先安排 GPU 的独立工作', 'Step 后立刻 renderer_.Gpu，把计算和粒子绘制写入命令流。CPU 随后进入 Jobs::Run 更新 actors_，给前面 GPU 工作与 CPU 工作重叠的机会。但运行时/驱动可能暂存命令，调用顺序本身不保证硬件立刻启动。'),('CPU 工作汇合后处理旧样本', 'Jobs::Run 返回时所有 actors_ 更新完成。ring_.Poll 尝试消费旧数据，Enqueue 安排当前 GPU 状态的回读；满环允许丢样。m.updateMs 只包围 CPU 任务执行和汇合，不是整个并行分支。'),('mode 1 把更新和等待放到关键路径', '先单线程 UpdateParticles，再安排 GPU 更新和绘制，随后 CopyResource 并立即 Map。CPU 在拿到当前 GPU 数据前不能继续走到后面的演员上传。这样把串行 CPU 更新与立即回读等待都放入当前帧流程。'),('共同末尾必须看', '两个分支结束后都执行 renderer_.Cpu(gpu,actors_,m)：把 CPU 对象上传并绘制。于是每帧两份数据都更新、两次绘制都存在，不能把 mode 0 当作少画一份。最终 Validate 分别检查 CPU 对象、GPU 粒子与回读样本。')],
 example='把一帧写成依赖链：CPU 对象更新 → 上传 → CPU 对象绘制；GPU 粒子更新 → GPU 粒子绘制，且更新 → 复制 → 完成后读样本。两条更新链之间没有箭头，所以能尝试重叠。假设独立 CPU 更新耗时 C、GPU 工作耗时 G，它们完全重叠的理想区间是 max(C,G)；整个帧还包括上传、提交、绘制和呈现，不能直接把 max(C,G) 当完整帧时间。',
 experiment='固定 count 和 workers，对照两种模式的总 CPU 帧耗时、GPU 区间、update_ms、readback_ms、帧龄及丢样。再在 mode 0 下只改 workers，观察 CPU 任务变化。两模式同时改变 CPU 调度与回读策略，所以总收益不能全归因于其中一项；结合第 04、06 章的单因素实验解释结果。',
 pitfalls='两种模式的核心更新和绘制量相同，但样本消费时效和频率不同：mode 0 可延迟或丢样，mode 1 每帧要当前结果。若真实游戏中 actors_ 必须立刻依赖本帧 GPU 碰撞结果，两条链就不再独立，不能照抄这个顺序。优化前先画数据依赖，比先加线程更重要。',
 completion='能独立画出本章依赖图，说明每处等待为何存在，并写出性能结论的适用条件，而不只说“并行更快”。'),
}


def render_foundations(esc, table):
    rows = [(esc(title), esc(body)) for title, body in FOUNDATIONS]
    return '<section class="panel searchable" id="reading-basics" data-title="入门带读 C++ DX11 如何读工程"><p class="eyebrow">BEFORE CHAPTER 01</p><h2>第一次读这些工程：先补齐这九件事</h2><p>先读这一节，再进入第 01 章。每章按“场景 → 数据 → Frame 原码 → 逐段解释 → 手算 → 实验”学习。暂时不必把公共框架全部读完。</p>' + table(['阅读问题', '把概念放回本工程'], rows) + '</section>'


def frame_source(text):
    start = text.index('    void Frame(')
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return '\n'.join(line[4:] if line.startswith('    ') else line for line in text[start:end].splitlines())


READING_ROUTES = {
    '01': [('src/common/gpu.cpp', 'void Device::WaitIdle', '等待的实现：EVENT、Flush 与轮询'), ('src/common/particles.cpp', 'void ParticleRenderer::Gpu', '绘制的实现：绑定 SRV 后 Draw')],
    '02': [('src/common/gpu.cpp', 'void GpuTimer::Begin', '记录起始时间戳'), ('src/common/gpu.cpp', 'void GpuTimer::Poll', '查询、换算与归属旧帧')],
    '03': [('src/common/particles.cpp', 'void ParticleRenderer::Cpu', '进入真正的 Map / memcpy / Draw 循环')],
    '04': [('src/common/gpu.cpp', 'void Jobs::Run', '派发任务并等待全部完成'), ('src/common/particles.cpp', 'void UpdateParticles', '每个工作线程执行的更新算法')],
    '05': [('src/common/particles.cpp', 'void ComputeParticles::Step', 'CPU 设置参数与 Dispatch'), ('shaders/particles.hlsl', 'void UpdateCS', 'GPU 真正执行的逐粒子算法')],
    '06': [('src/common/particles.cpp', 'void ReadbackRing::Enqueue', '提交复制并标记源帧'), ('src/common/particles.cpp', 'void ReadbackRing::Poll', '检查完成后读取')],
    '07': [('src/common/particles.cpp', 'void ParticleRenderer::GridState', '每个 Context 必须绑定的绘制状态'), ('src/common/gpu.cpp', 'void Jobs::Run', '录制任务的汇合点')],
    '08': [('src/common/particles.cpp', 'void ComputeParticles::Step', '独立的 GPU 更新链'), ('src/common/gpu.cpp', 'void Jobs::Run', 'CPU 对象更新链'), ('src/common/particles.cpp', 'void ReadbackRing::Poll', '延迟消费链')],
}


def extract_function(text, needle):
    start = text.index(needle)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    if needle == 'void UpdateCS':
        start = text.rfind('[numthreads', 0, start)
    return text[start:end]


def render_walkthrough(n, path, source, esc, block, srclink, sources):
    d = LESSONS[n]
    parts = [f'<div class="walkthrough" id="ch{n}-guide"><p class="eyebrow">GUIDED WALKTHROUGH / {n}</p><h3>1．先想清楚：这个工程在演示什么？</h3><p>{esc(d["scenario"])}</p>',
             f'<h3>2．数据放在哪里，谁能修改它？</h3><p>{esc(d["ownership"])}</p>',
             '<h3>3．对着真实 Frame 逐段读</h3><p>下面直接摘自当前章节源码。先找模式分支和共同末尾，再看编号解释。函数里调用的公共实现可通过本章源码入口继续追踪。</p>',
             block(frame_source(source), 'C++ · 当前工程 Frame 原码'), srclink(path, 'void Frame(', '在完整源码中定位 Frame')]
    for i, (title, body) in enumerate(d['steps'], 1):
        parts.append(f'<article class="read-step"><h4><span>{i:02}</span> {esc(title)}</h4><p>{esc(body)}</p></article>')
    parts.append('<details class="implementation"><summary>继续追踪：展开本章调用的公共函数原码</summary><p>先读懂上面的 Frame，再按下面顺序读。这里是当前源文件的真实函数片段，完整文件仍可由链接展开。</p>')
    for target, needle, label in READING_ROUTES[n]:
        parts.append('<h4>'+esc(label)+'</h4>'+srclink(target, needle, target)+' '+block(extract_function(sources[target], needle), 'HLSL' if target.endswith('.hlsl') else 'C++'))
    parts.append('</details>')
    for title, field in [('4．拿一组具体数字手算', 'example'), ('5．动手实验：改什么、看什么、如何解释', 'experiment'), ('6．容易误解的地方', 'pitfalls')]:
        parts.append(f'<h3>{title}</h3><p>{esc(d[field])}</p>')
    parts.append(f'<div class="note"><strong>这一章学会的标准：</strong>{esc(d["completion"])}</div></div>')
    return '\n'.join(parts)
