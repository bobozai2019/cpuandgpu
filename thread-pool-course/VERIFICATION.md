# 本机验证记录

日期：2026-10-28（Asia/Shanghai）。

## 编译与运行

- Windows x64，Visual Studio 2026 / MSVC 19.51.36260.0；.NET SDK 10.0.402。
- C++17 与 C# 两个工程均已在 **Debug、Release** 下成功构建；构建输出无编译警告或错误。
- 两种配置下，每份工程的 **7/7 组契约测试通过**。
- 两种配置下，两种语言均运行 `workers=1` 和 `workers=4`、`tasks=17`、`iterations=10000` 的全部实验。
- 平方和均为 **1496**；最终 completed 为 **19**；异常被传回，后续任务正常执行，关闭后提交被拒绝。
- mutex/lock 和 atomic/Interlocked 模式计数均为 **170000**。
- 串行与池内 CPU 工作的校验值均为 **5343742284330204049**。
- Debug/Release 的程序与日志保存在本目录的 `cpp/build`、`csharp/bin`、`validation` 中。

复现命令（从本目录执行）：

```powershell
.\scripts\test.ps1 -Configuration Release
.\scripts\test.ps1 -Configuration Debug
```

测试覆盖：返回值与异常传播、多生产者任务恰好执行一次、固定并发度、空闲等待、队列空但任务仍执行、并发关闭与提交竞态、排空与拒绝、重复关闭、worker 自身等待保护、空池、非法线程数、析构/Dispose 排空。C++ 另覆盖可移动不可复制的 callable；C# 另覆盖同步委托契约与空委托。

## 离线教材

使用已安装的 Chrome，通过 Playwright 以 `file://` 打开页面并关闭网络访问。两份页面均通过：

- 内部锚点、源码行号、本地文件链接存在且没有重复 ID。
- 内嵌的 4 份源码 SHA-256 与各自工程真实文件一致。
- 队列提交、关闭后拒绝、继续排空、停止和重置。
- 拆分读写得到 1；原子和 mutex 模型得到 2。
- 粒度计算器默认结果与非法输入处理。
- 搜索结果、关闭搜索、展开源码并跳到目标行。
- 1440px 桌面和 390px 窄屏布局；窄屏页面无整体横向溢出。
- 浏览器没有 JavaScript 运行错误。

页面截图位于 `validation/*-desktop.png`、`*-mobile.png`、`*-source.png`、`*-simulator-mobile.png`。已人工查看桌面与窄屏截图。

维护时可运行：

```powershell
python scripts/generate_lessons.py
python scripts/verify_lessons.py
```

页面检查脚本需要 Python Playwright 和 Chrome/Edge；普通阅读与工程运行不需要它们。打印样式已提供，未对所有打印机、浏览器或纸张组合逐一验证。

## 隔离范围与验证边界

本次创建/编辑的课程文件、编译目标、源码快照及验证输出均位于 `thread-pool-course`。C++ 的 CMake 缓存明确指向 `thread-pool-course/cpp`，C# 构建指向本目录的独立 csproj；没有挂接原课程的根 CMake、源码或启动脚本。

开始时记录了现有文件元数据。结束时观察到原目录的 `CMakeLists.txt`、`README.md`、`tests/gui_controls.cpp` 以及原有构建产物在本会话期间发生外部变化；这些文件未被本次操作写入，也未回滚。因此没有把“整个工作区字节不变”作为本次验证结论。

并发测试不能证明所有调度下都没有缺陷；未运行 ThreadSanitizer，也未在 Linux/macOS 上实际构建。性能输出仅供本机实验观察，不用于宣称线程池必然更快或比较语言优劣。
