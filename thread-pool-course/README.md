# 线程池实现课程 · C++17 与 C# / .NET 10

新增的独立课程，交付 **两份可运行工程 + 两份中文教学 HTML**。本目录内自行构建和运行，不修改或引用原有 8 章课程的源代码、手册、入口及构建文件。

先双击 **Start-Learning.cmd**，或直接打开教材：

| 语言 | 教学 HTML | 工程入口 | 线程池实现 |
|---|---|---|---|
| C++17 | [cpp/lesson.html](cpp/lesson.html) | [cpp/CMakeLists.txt](cpp/CMakeLists.txt) | [thread_pool.hpp](cpp/include/thread_pool.hpp) |
| C# / .NET 10 | [csharp/lesson.html](csharp/lesson.html) | [ThreadPoolCourse.sln](csharp/ThreadPoolCourse.sln) | [FixedThreadPool.cs](csharp/FixedThreadPool.cs) |

两份教材均为单文件 HTML：离线阅读、搜索、复制命令、源码行号跳转、打印均可用；包含线程池状态模拟、加法交错模拟、任务粒度计算器和练习答案。交互演示是逻辑模型，真实线程实验在工程中运行。

## 教学内容

1. 线程、任务、并发、并行、线程复用和 FIFO 队列。
2. 数据竞争、读改写、原子性、可见性、内存顺序、`std::atomic` / `Interlocked` / `volatile` 的区别。
3. `std::mutex`、RAII、`lock_guard`、`unique_lock`；C# `lock(object)` / `Monitor` 与 `System.Threading.Mutex`。
4. 条件变量、谓词等待、通知与状态、避免丢失唤醒；C# 共用等待集合为什么用 `PulseAll`。
5. 从构造、提交、取任务、锁外执行、结果/异常到完成记账的源码带读。
6. 空闲等待、优雅关闭、并发关闭、对象生命周期、嵌套等待死锁和工程边界。
7. 三个可运行实验、参数对照、校验输出、7 组契约测试和扩展练习。

## 构建与运行

Windows：双击 **Build-All.cmd** 构建两份工程，双击 **Test-All.cmd** 构建并验证。需要 Visual Studio 2022/2026 C++ 工具、CMake 3.24+ 和 .NET 10 SDK。脚本可查找 Visual Studio 自带的 CMake；不自动安装或升级系统依赖。

在本目录打开 PowerShell：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1

.\cpp\build\Release\thread_pool_demo.exe --demo all --workers 4 --tasks 17 --iterations 10000
dotnet run --project .\csharp\ThreadPoolCourse.csproj -c Release -- --demo all --workers 4 --tasks 17 --iterations 10000

powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test.ps1 -SkipBuild
```

两个工程可分别构建：

```powershell
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure

dotnet build csharp/ThreadPoolCourse.csproj -c Release
dotnet run --project csharp/ThreadPoolCourse.csproj -c Release -- --test
```

C++ 可在 Visual Studio 中打开 `cpp` 的 CMake 文件夹，或打开 `cpp/build` 下生成的 `.sln` / `.slnx`。C# 可直接打开 `csharp/ThreadPoolCourse.sln`。

C++ 实现只使用标准库；Linux/macOS 可用 `cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release`，随后构建并运行 `cpp/build/thread_pool_demo`。跨平台运行尚未在本机验证；`.cmd` 和配套 PowerShell 启动/测试脚本面向 Windows。

## 三个实验

| 参数 | 内容 | 检查结果 |
|---|---|---|
| `--demo pool` | 平方求和、业务异常、worker 存活、关闭后拒绝 | `--tasks 17` 时和为 1496，最终完成数为 19 |
| `--demo counter` | mutex/lock 与 atomic/Interlocked 两种安全计数 | 两种模式均为 tasks × iterations |
| `--demo benchmark` | 串行与池内执行相同 CPU 工作，比较校验值 | 两种模式 checksum 相同，打印实测毫秒 |

默认 `--demo all --workers 4 --tasks 16 --iterations 20000`。线程数范围 1..64，任务数 1..10000，每任务迭代数 1..1000000。`--help` 查看帮助；参数错误返回非零退出码。

计时排除池的创建，包含提交与结果汇合；短预热不能消除所有缓存/JIT/调度影响。用 Release，固定输入，多次比较；正确性测试不要求并行一定更快。两个语言的程序不是严谨的跨语言基准。

## 实现契约

- 固定工作线程，共用无界 FIFO 队列；FIFO 取出不保证执行或完成顺序。
- 允许多个生产者并发提交，支持返回值、无返回值任务和业务异常传播。
- 用户任务在队列锁外执行；异常任务也记入 completed。
- 一致快照满足 `submitted = queued + active + completed`；单独完成计数只用于进度显示。
- `WaitIdle/wait_idle` 观察队列空且执行中任务为零，不阻止后续提交。
- `Shutdown/shutdown` 停止接收，排空已接收任务，Join 所有线程；外部重复与并发调用均可。
- 禁止同池工作线程调用空闲等待或关闭，防止自我等待；未检测任意任务之间的依赖环。
- C++ 析构及 C# Dispose 由池外所有者完成；对象销毁时须协调其他调用者停止访问。
- 只执行同步任务。C# 检查常见 Task/ValueTask 返回类型及直接 async void 委托；不要用包装绕过契约。它不流动 ExecutionContext，不是内置 ThreadPool 的替代品。
- 不提供取消、优先级、背压、工作窃取或强制中止。任务不返回时，关闭会一直等待。

## 文件结构

```text
thread-pool-course/
  Start-Learning.cmd           学习菜单
  Build-All.cmd / Test-All.cmd 构建 / 验证入口
  cpp/
    CMakeLists.txt             独立 C++17 工程
    include/thread_pool.hpp    线程池实现
    src/main.cpp              三个实验
    tests/thread_pool_tests.cpp
    lesson.html               C++ 中文教材
    build/                    本课程 C++ 构建输出
  csharp/
    ThreadPoolCourse.sln / ThreadPoolCourse.csproj
    FixedThreadPool.cs         线程池实现
    Program.cs                三个实验
    SelfTests.cs              契约验证
    lesson.html               C# 中文教材
    bin/ / obj/               本课程 C# 构建输出
  scripts/
    build.ps1 / test.ps1
    generate_lessons.py        把真实源码嵌入两份教材
    verify_lessons.py          可选：离线页面与交互检查
  validation/                 测试日志与页面截图（自动生成）
```

修改源码或教材后运行 `python scripts/generate_lessons.py` 更新两个 HTML。生成器仅依赖 Python 标准库，阅读与运行工程不需要 Python。页面验证脚本另需 Python Playwright 和已安装的 Chrome/Edge，仅用于维护时检查。

具体的本机验证结果见 [VERIFICATION.md](VERIFICATION.md)。
