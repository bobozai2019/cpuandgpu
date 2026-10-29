# 00 工程导览：先知道一帧从哪里开始

## 本章教什么

目标：能从程序入口追踪到一次 GPU 绘制，知道每类代码由谁执行；能运行、切模式、找到指标。

具体内容：Win32 窗口与离屏目标、Device / Context、顶点着色器与像素着色器、CPU 数组与 GPU Buffer、共享框架的职责。

## 最小前置知识

理解 C++ 的函数、数组、结构体、指针、生命周期和基本循环即可开始。学习第 4 章时再补线程、互斥量、条件变量；第 5 章再补 HLSL。不要先逐行研究所有 Win32 消息处理。

## 从入口追踪

1. 每章 `.cpp` 的 `main` 创建该章的 `Lesson`，交给 `Run`。
2. `Run` 解析参数，建立窗口或离屏目标，调用 `Device::Init`。
3. `Lesson::Init` 创建资源；创建资源属于初始化，不把这段耗时算进每帧数据。
4. 主循环清屏 → 开始时间戳 → `Lesson::Frame` → 结束时间戳 → `Present`（离屏为 Flush）。
5. 后续帧轮询旧时间戳；旧结果填回其所属帧的 CSV 行。
6. 有限帧运行结束后等待 GPU，执行验证和截图。这些退出时的阻塞不计入实验帧。

`ID3D11Device` 创建资源和着色器；`ID3D11DeviceContext` 设置状态、更新资源、发出 Draw/Dispatch。标准配置下 Device 可供多线程使用；同一个 Context 同时只允许一个线程调用。本工程只在第 7 章让每个工作线程使用独立 Deferred Context。

窗口和离屏都先画到独立纹理；窗口模式在 Present 前将纹理复制到交换链。这样最后一帧截图不依赖 DISCARD 交换链在呈现后是否保留内容。这个固定显示复制位于本章 GPU 计时区间之外，因此不要把记录的 GPU 区间误认为包含整个显示链路。

## 图像是怎么来的

粒子是四个 float：位置 `(x,y)`、速度 `(vx,vy)`，共 16 字节。CPU 版本把它当顶点缓冲上传；GPU 版本把它保存在 StructuredBuffer，通过顶点着色器的 `SV_VertexID` 读取。每个粒子画一个点，颜色由位置计算。

第 7 章使用三角形网格：每次 Draw 画一个三角形，目的是放大 CPU 命令录制成本，而不是追求美术效果。所有场景不需要外部模型、图片或素材下载。

## 实际操作

```powershell
.\scripts\build.ps1
.\scripts\run.ps1 -Chapter 1
.\scripts\run.ps1 -Chapter 5
```

先用默认参数看到窗口。按 M 后观察模式名称变化、粒子重新开始；空格暂停，V 切换垂直同步。随后用 `--frames 120 --output out\first.csv` 运行一次有限帧实验。

## 完成标准与练习

- 能指出哪些 `.cpp` 运行在 CPU，哪些 `.hlsl` 函数运行在 GPU。
- 能找到调用 `Draw` 的地方，并解释为什么函数返回不表示画面已显示。
- 能独立定位 `Info()`、`Init()`、`Frame()` 和 `Validate()`。
- 练习：改变 HLSL 的 `MakeParticle` 颜色，重新运行；HLSL 在启动时编译，修改根目录 `shaders` 后从项目根目录运行即可。若从 build 目录运行，重新执行构建脚本以更新构建目录的着色器副本。

参考：[DX11 Device 与 Context](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-intro)。
