// 本文件是各个课堂共用的底层实现，声明集中在 classroom.h 中。
// 阅读顺序：错误/编码工具 → Device 设备与图像 → GpuTimer 异步计时 → Jobs CPU 线程池。
// Device::Init 创建资源；每帧 Begin 清屏、课堂代码绘制、Present 显示；退出时可验证或截图。
// device 负责创建 GPU 资源，context 负责设置流水线状态和发出绘制/复制/查询命令。
// CPU 调用图形 API 返回、命令提交、GPU 执行完成是三个不同的时刻。
// ComPtr<T> 是 COM 智能指针：管理 AddRef/Release 引用计数；Get() 借用底层指针。
// 下文保留原有语句；注释中的“限制/前提”说明现有实现，不表示已经增加额外检查。
// 对照文档（Microsoft Learn）：
// https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata
// https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_query
// https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush
// https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map
// https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicbitmapframeencode-setpixelformat

// 引入本项目公共声明，包括 Device、GpuTimer、Jobs，以及 Windows/D3D11 和标准库类型。
#include "classroom.h"
// 引入 Windows Imaging Component（WIC）接口，后面的 Capture 用它把像素编码成 PNG。
#include <wincodec.h>

// 打开 classroom 命名空间；这里的函数和类成员属于 classroom，避免污染全局名称。
namespace classroom {
// 统一检查 Windows/COM/D3D 的 HRESULT 返回码；operation 是用于报错的操作名称。
// void 表示不返回值；成功时直接走到函数末尾，失败时抛出异常。
void Check(HRESULT hr, const char *operation) {
    // FAILED 判断 HRESULT 是否表示失败（按有符号值判断小于 0）。
    // S_FALSE 也是成功类状态码，不会进入此分支；查询代码用它表示“结果尚未就绪”。
    if (FAILED(hr)) {
        // 创建字符串输出流 s，用流式拼接构造错误文本；这是 CPU 上的普通 C++ 对象。
        std::ostringstream s;
        // 依次写入操作名、固定提示和错误码；std::hex 使后续整数按十六进制输出。
        // static_cast<uint32_t> 把 HRESULT 转为 32 位无符号值，便于显示完整错误码位模式。
        s << operation << " failed: HRESULT 0x" << std::hex << static_cast<uint32_t>(hr);
        // s.str() 取出拼好的 std::string；throw 抛出 runtime_error，交给上层异常处理逻辑。
        throw std::runtime_error(s.str());
    }
}
// 把 UTF-8 字节字符串转换为 Windows 使用的 UTF-16 宽字符串。
// const std::string & 表示只读引用，避免复制输入；Windows 上 wchar_t 是 16 位代码单元。
std::wstring Wide(const std::string &s) {
    // 先判断输入是否为空；这个 if 没有大括号，只控制紧随其后的 return 语句。
    if (s.empty())
        // 返回一个空 std::wstring；{} 根据函数返回类型进行值初始化。
        return {};
    // 第一次转换只查询所需长度：CP_UTF8 指定输入编码，0 表示默认转换选项。
    // s.data()/s.size() 提供字节地址和字节数；显式长度不包含尾部终止符。
    // 输出地址 nullptr、容量 0 表示不实际写入；n 得到所需 wchar_t 数量，不是 Unicode 字符个数。
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    // 创建长度为 n 的宽字符串并用宽空格 L' ' 填充，为实际转换准备可写缓冲区。
    std::wstring result(n, L' ');
    // 第二次转换把 UTF-16 代码单元写入 result.data()，最多写 n 个；C++17 支持可写 data()。
    // 现有实现没有检查两次转换的失败返回值，适用于长度可表示为 int 的正常输入。
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n);
    // 返回转换结果；编译器可采用返回值优化或移动，避免多余的字符串复制。
    return result;
}
// 做反向转换：把 UTF-16 宽字符串变为 UTF-8 字节字符串，便于写控制台或文件。
std::string Utf8(const std::wstring &s) {
    // 空输入无需调用 Windows 转码 API。
    if (s.empty())
        // 按返回类型构造并返回空 std::string。
        return {};
    // 先查询 UTF-8 输出所需字节数；s.size() 是输入 UTF-16 代码单元数。
    // 输出地址 nullptr、容量 0 表示只计算大小；CP_UTF8 为目标编码，0 为默认标志。
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                // 最后两个 nullptr 分别对应默认替代字符和“是否用了替代字符”的输出参数；UTF-8 要求它们为空。
                                nullptr, nullptr);
    // 分配 n 字节的字符串缓冲区，先填空格，随后由转换结果覆盖。
    std::string result(n, ' ');
    // 执行实际的 UTF-16 → UTF-8 转换，写入 result.data()，缓冲区字节容量为 n。
    // 倒数第二个 nullptr 不提供默认替代字符；现有代码同样未检查转换返回值。
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr,
                        // 最后的 nullptr 不接收默认字符使用标记；此行结束上一行的函数调用。
                        nullptr);
    // 返回 UTF-8 字节串；字符串对象自行管理存储及末尾的空字符。
    return result;
}
// 初始化图形设备。viewport 是输出窗口句柄 HWND，空句柄表示无窗口模式；o 是只读运行选项。
// Device:: 表示这是 Device 类的成员函数，device/context/width 等未加前缀的名字是其成员。
void Device::Init(HWND viewport, const Options &o) {
    // 声明 DXGI 工厂的智能指针；工厂用于枚举显卡适配器和创建交换链，此时指针为空。
    ComPtr<IDXGIFactory1> factory;
    // 创建 IDXGIFactory1；IID_PPV_ARGS 根据输出指针类型生成接口 IID 和接收地址。
    // &factory 使用 ComPtr 的输出参数支持接收 COM 指针；Check 在创建失败时抛异常。
    Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    // 保存最终选中的硬件适配器；暂时为空，WARP 模式下将继续保持为空。
    ComPtr<IDXGIAdapter1> selected;
    // 记录当前候选中最大的专用显存字节数；SIZE_T 是与指针宽度匹配的无符号大小类型。
    SIZE_T best = 0;
    // !o.warp 表示没有指定软件渲染模式，此时需要寻找硬件适配器。
    if (!o.warp) {
        // 用 UINT 无符号整数 i 从 0 开始枚举；for 中间条件省略，退出由下面的 break 决定。
        for (UINT i = 0;; ++i) {
            // 为本轮枚举创建候选适配器智能指针，每轮离开作用域时自动释放其持有的引用。
            ComPtr<IDXGIAdapter1> candidate;
            // 请求索引 i 对应的适配器，把接口写入 candidate，把返回状态保存到 hr。
            HRESULT hr = factory->EnumAdapters1(i, &candidate);
            // DXGI_ERROR_NOT_FOUND 表示没有更多适配器，这是正常枚举终点。
            if (hr == DXGI_ERROR_NOT_FOUND)
                // 退出枚举循环；不把“枚举结束”传给 Check 当成致命错误。
                break;
            // 除“没有更多适配器”外，其他失败都由 Check 抛出异常。
            Check(hr, "EnumAdapters1");
            // 用 {} 把适配器描述结构置零，其中包含名称、显存大小和适配器标志。
            DXGI_ADAPTER_DESC1 desc{};
            // 把候选适配器的信息写入 desc；现有代码没有检查 GetDesc1 的 HRESULT。
            candidate->GetDesc1(&desc);
            // 按位 & 检测软件适配器标志，再用 ! 取反，要求候选必须不是软件适配器。
            // && 会短路：左侧不满足时，无需计算下一行的显存比较。
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                // 尚无已选适配器，或者候选的专用显存更多，就替换选择；同容量时保留先遇到的。
                // 这是简单的选择策略，专用显存大小并不能严格代表 GPU 计算性能。
                (!selected || desc.DedicatedVideoMemory > best)) {
                // 更新当前最大专用显存容量，供后续适配器比较。
                best = desc.DedicatedVideoMemory;
                // 复制 ComPtr，保留当前候选的 COM 引用；旧 selected 引用由智能指针管理释放。
                selected = candidate;
                // 把设备描述中的宽字符名称复制到 Device::adapter，供日志和界面显示。
                adapter = desc.Description;
            }
        }
        // 枚举结束后仍为空，说明没有选到符合条件的硬件适配器。
        if (!selected)
            // 抛出带操作提示的异常；--warp 可以改用 CPU 软件实现做功能验证。
            throw std::runtime_error("No hardware adapter. Try --warp for functional validation.");
    // 进入 o.warp 为真的分支；没有大括号的 else 只控制下一条赋值语句。
    } else
        // 记录 WARP 的可读名称；WARP 在 CPU 上实现 D3D，测得时间不代表物理 GPU 的性能。
        adapter = L"WARP (software rasterizer; not a performance reference)";
    // 设置设备创建标志，启用 BGRA 格式支持，和本文件采用的 BGRA 图像格式配合。
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    // 检查命令行选项是否要求启用 D3D11 调试层。
    if (o.debug)
        // |= 按位加入 DEBUG 标志而保留已有 BGRA 标志；系统需安装对应的 Graphics Tools。
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    // 只请求 Feature Level 11_0；它描述设备功能等级，不是着色器或驱动的版本号。
    const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
    // 准备接收实际创建成功的功能等级，{} 先将变量值初始化。
    D3D_FEATURE_LEVEL actual{};
    // 创建 D3D11 设备：Get() 借用 selected 指针；?: 根据 o.warp 选择驱动类型。
    // WARP 使用空适配器和 WARP 类型；显式指定硬件适配器时必须使用 UNKNOWN 类型。
    Check(D3D11CreateDevice(selected.Get(), o.warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_UNKNOWN,
                            // nullptr 不提供自定义软件光栅器模块；flags 是创建选项。
                            // &requested、1 表示功能等级数组仅有一个元素；SDK_VERSION 指定 SDK 接口版本。
                            // &device 接收设备接口，&actual 接收实际功能等级；不满足要求会创建失败。
                            nullptr, flags, &requested, 1, D3D11_SDK_VERSION, &device, &actual,
                            // &context 接收设备的 immediate context（即时上下文），用于提交本程序的图形命令。
                            // 即时上下文并不意味着每条命令立即执行完毕；这里也没有为它启用并发调用保护。
                            &context),
          // 把 D3D11CreateDevice 的返回值交给 Check；附带调试层缺失时的排查提示。
          "D3D11CreateDevice (install Graphics Tools if --debug fails)");
    // As 通过 QueryInterface 尝试取得 ID3D11InfoQueue 调试消息队列；SUCCEEDED 判断是否成功。
    // debugActive 因此表示实际取得了调试接口，而不是仅仅请求过 --debug。
    debugActive = SUCCEEDED(device.As(&infoQueue));
    // 只有提供了窗口句柄，才创建负责把图像送到窗口的交换链。
    if (viewport) {
        // 把交换链描述置零；未显式赋值的刷新率、采样质量和其他字段保留零值。
        DXGI_SWAP_CHAIN_DESC sd{};
        // 设置交换链缓冲图像宽度，单位是像素；width 来自 Device 成员。
        sd.BufferDesc.Width = width;
        // 设置交换链缓冲图像高度，单位是像素。
        sd.BufferDesc.Height = height;
        // 每个像素按 B、G、R、A 排列，各占 8 位；UNORM 表示映射到 [0,1] 的无符号归一化值。
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        // 每像素只取一个样本，即不启用 MSAA 多重采样；采样质量保持初始化时的 0。
        sd.SampleDesc.Count = 1;
        // 声明交换链缓冲区可用作渲染目标；本程序实际上先画离屏纹理，再复制到这里。
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        // 请求两个后备缓冲区；数量本身不等于允许 CPU 领先 GPU 的固定帧数。
        sd.BufferCount = 2;
        // 指定图像呈现到哪个窗口。
        sd.OutputWindow = viewport;
        // TRUE 指定窗口模式，不使用独占全屏模式。
        sd.Windowed = TRUE;
        // 采用传统 DISCARD 交换效果，Present 后不能假定后备缓冲里的旧图像仍然有效。
        // 它不同于名字里包含 FLIP 的现代翻转模型，这也是另建稳定截图纹理的原因。
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        // 按 sd 创建交换链并保存到成员 swapchain；失败时抛异常。
        Check(factory->CreateSwapChain(device.Get(), &sd, &swapchain), "CreateSwapChain");
        // 关闭 DXGI 自动响应 Alt+Enter 切换全屏的行为；当前代码没有检查此调用的返回码。
        factory->MakeWindowAssociation(viewport, DXGI_MWA_NO_ALT_ENTER);
        // 取得第 0 个交换链后备缓冲的 ID3D11Texture2D 接口，保存到 swapBuffer 供复制使用。
        Check(swapchain->GetBuffer(0, IID_PPV_ARGS(&swapBuffer)), "GetBuffer");
    }
    // 开启一个局部作用域，把下面只用于创建纹理的 td 限制在此块内；不是条件分支。
    {
        // 保留独立渲染目标，使 Present 后的最终截图不依赖 DISCARD 交换链内容。
        // 离屏渲染纹理描述清零；Usage 的零值是 D3D11_USAGE_DEFAULT，默认没有 CPU 读写权限。
        D3D11_TEXTURE2D_DESC td{};
        // 离屏纹理宽度与交换链一致，保证后面的整资源复制尺寸匹配。
        td.Width = width;
        // 离屏纹理高度与交换链一致。
        td.Height = height;
        // 仅创建一个 mip 级别（原始分辨率），不创建逐级缩小的纹理链。
        td.MipLevels = 1;
        // 数组中仅含一张二维纹理，不创建纹理数组。
        td.ArraySize = 1;
        // 离屏目标也使用 32 位 BGRA UNORM，与交换链和截图缓冲的像素布局一致。
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        // 单样本纹理，无需多采样解析就能复制或进行后续读回。
        td.SampleDesc.Count = 1;
        // 允许此纹理作为渲染目标绑定；未设置 SHADER_RESOURCE，因此不用于着色器采样。
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        // 创建纹理到 target；nullptr 表示不提供初始像素，内容随后由 Begin 中的清屏初始化。
        // 无窗口模式也会执行这一段，因此自动测试同样能渲染和验证图像。
        Check(device->CreateTexture2D(&td, nullptr, &target), "Create offscreen target");
    }
    // 为 target 创建 RTV（渲染目标视图）；nullptr 让 D3D 按纹理描述推导默认视图。
    // 纹理负责保存像素，视图描述流水线如何访问它；后面绑定的是 rtv。
    Check(device->CreateRenderTargetView(target.Get(), nullptr, &rtv), "Create RTV");
}
// 把离屏渲染目标和视口设置到指定上下文；ctx 可以是即时上下文或课堂创建的延迟上下文。
void Device::BindTarget(ID3D11DeviceContext *ctx) {
    // auto 推导为 ID3D11RenderTargetView*；Get() 不转移所有权，也不额外增加 COM 引用。
    auto view = rtv.Get();
    // 在 OM（输出合并）阶段绑定一个颜色目标；&view 是指针数组首地址，nullptr 表示不绑定深度/模板视图。
    ctx->OMSetRenderTargets(1, &view, nullptr);
    // 依次指定视口左上角 x/y、宽/高、最小/最大深度；深度范围为 [0,1]。
    // static_cast<float> 把整型像素尺寸转换为视口结构要求的浮点数。
    D3D11_VIEWPORT vp{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
    // 把这一个视口交给 RS（光栅化）阶段，确定标准化坐标映射到目标图像的位置和大小。
    ctx->RSSetViewports(1, &vp);
}
// 开始一帧的公共渲染准备；这里的 Begin 是项目方法，与 D3D 查询对象的 Begin 不同。
void Device::Begin() {
    // 在成员即时上下文上绑定离屏目标和视口，保证后续绘制写到 target。
    BindTarget(context.Get());
    // 定义 RGBA 顺序的清屏浮点颜色：很暗的蓝色背景，alpha=1 表示完全不透明。
    // 注意 API 的颜色参数始终按 RGBA 给出，和纹理内存的 BGRA 字节顺序不是同一概念。
    const float clear[] = {0.018f, 0.028f, 0.055f, 1};
    // 发出清除整个 RTV 的命令；GPU 执行后像素才更新，函数返回不等于 GPU 清屏已经完成。
    context->ClearRenderTargetView(rtv.Get(), clear);
}
// 完成一帧的呈现/提交；vsync 决定交换链呈现时是否请求等待垂直刷新节拍。
void Device::Present(bool vsync) {
    // 存在交换链说明当前是窗口模式，需要把离屏结果交给显示系统。
    if (swapchain) {
        // 把 target 完整复制到 swapBuffer，参数顺序为目标、源；这是排入上下文的 GPU 复制命令。
        // 两者尺寸、格式和采样配置兼容，且复制不经过 CPU 像素缓冲。
        context->CopyResource(swapBuffer.Get(), target.Get());
        // vsync 为真时 SyncInterval=1，为假时为 0；第二个 0 表示不使用额外呈现标志。
        // Present 可能因显示节拍或队列限制而阻塞 CPU，但不能拿它代替 GPU 完成查询。
        Check(swapchain->Present(vsync ? 1 : 0, 0), "Present");
    // 没有交换链时进入无窗口路径，不尝试把图像呈现到屏幕。
    } else
        // 显式把缓冲的命令提交出去；Flush 自身不保证 GPU 已完成这些命令。
        context->Flush(); // 无 Present 的自动测试需要推动命令提交；这里不等待完成。
}
// 阻塞 CPU，直到此即时上下文中插入的 EVENT 之前的 GPU 工作完成。
// 名字叫 WaitIdle，但它不是等待全系统所有 GPU 或其他进程空闲；正常每帧调用会破坏 CPU/GPU 重叠。
void Device::WaitIdle() {
    // 创建 EVENT 类型的查询描述，MiscFlags=0；EVENT 是完成标记，不是 Windows 线程事件句柄。
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
    // 用智能指针保存查询对象，离开函数后自动释放本地持有的 COM 引用。
    ComPtr<ID3D11Query> query;
    // 在当前设备上创建查询对象；此时只是分配查询资源，还没有插入完成标记。
    Check(device->CreateQuery(&desc, &query), "Create event query");
    // 把 EVENT 标记插入即时上下文命令序列；EVENT 不使用 Begin，只需要 End。
    context->End(query.Get());
    // 主动提交包括标记在内的命令，保证后面使用 DONOTFLUSH 轮询时仍能取得进展。
    context->Flush();
    // 记录 CPU 单调时钟起点，只用于计算等待超时，不是 GPU 时间戳。
    const auto start = Clock::now();
    // 无限轮询，完成时 break，出错或超时时 throw。
    for (;;) {
        // 只查是否完成：数据指针 nullptr 对应数据大小 0，不读取 BOOL 载荷。
        // DONOTFLUSH 禁止该轮询隐式触发提交；S_OK 为已完成，S_FALSE 为尚未就绪。
        HRESULT hr = context->GetData(query.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
        // 判断事件完成标记是否已经被 GPU 执行完成。
        if (hr == S_OK)
            // 跳出轮询，函数随后返回，调用方可以继续执行依赖此结果的 CPU 工作。
            break;
        // 传播设备丢失等真正错误；S_FALSE 不满足 FAILED，因此仍可继续等待。
        Check(hr, "WaitIdle GetData");
        // Ms(start) 返回 CPU 已等待的毫秒数，15000 即约 15 秒。
        if (Ms(start) > 15000)
            // 超时就抛异常，避免因设备或驱动异常无限等待。
            throw std::runtime_error("GPU wait timed out");
        // 让当前 CPU 线程睡眠约 1 毫秒，避免紧密空转占满核心；实际唤醒时刻取决于调度。
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
// 扫描 D3D 调试消息并返回警告及更严重消息的数量；返回类型是 32 位无符号整数。
uint32_t Device::DebugErrors() {
    // 如果没有取得调试队列接口，就无法读取调试层消息。
    if (!infoQueue)
        // 返回 0；这里代表“没有可统计的队列”，并不能据此证明图形调用都正确。
        return 0;
    // 初始化本次扫描的计数器。
    uint32_t errors = 0;
    // 按消息索引遍历队列当前存储的所有消息；UINT64 对应该 API 的 64 位消息索引。
    for (UINT64 i = 0; i < infoQueue->GetNumStoredMessages(); ++i) {
        // 准备接收某条消息所需的总缓冲字节数。
        SIZE_T size = 0;
        // 先传空消息地址查询所需容量，包括消息结构与描述文本；现有实现未检查这次查询的返回码。
        infoQueue->GetMessage(i, nullptr, &size);
        // 按查询得到的字节数分配临时缓冲区；每轮迭代结束后自动释放。
        std::vector<char> bytes(size);
        // 把字节缓冲首地址解释为 D3D11_MESSAGE*，让 API 把结构及后续数据写入该内存。
        auto *msg = reinterpret_cast<D3D11_MESSAGE *>(bytes.data());
        // 第二次调用实际读取消息，并检查是否成功；size 同时作为容量输入和实际大小输出。
        Check(infoQueue->GetMessage(i, msg, &size), "Read debug message");
        // 严重度枚举按 CORRUPTION、ERROR、WARNING、INFO、MESSAGE 排序，值越小越严重。
        // 因此 <= WARNING 同时选出损坏、错误和警告，不包括普通信息。
        if (msg->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
            // 把当前问题计入总数；变量虽然叫 errors，实际上也包含警告。
            ++errors;
            // 把描述文本写到标准错误流并换行；pDescription 指向当前消息缓冲中的文本。
            std::cerr << "D3D11 validation: " << msg->pDescription << '\n';
        }
    }
    // 返回计数；本函数没有清空队列，重复调用可能再次统计同一批消息。
    return errors;
}
// 从 HLSL 文件编译一个着色器，返回二进制 blob。
// file 是宽字符文件名，entry 是入口函数名，profile 是目标模型（例如 vs_5_0、ps_5_0、cs_5_0）。
ComPtr<ID3DBlob> Device::Compile(const wchar_t *file, const char *entry, const char *profile) {
    // 创建并清零固定长度宽字符数组，用来接收当前可执行文件的完整路径。
    wchar_t exe[MAX_PATH]{};
    // nullptr 表示当前进程的可执行模块，MAX_PATH 是数组容量。
    // 现有代码没有检查 API 失败或长路径截断，后续路径查找依赖这里获得有效路径。
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    // 把 exe 转成 filesystem::path，再取父目录，例如 build/Debug；const auto 禁止后续修改。
    const auto dir = std::filesystem::path(exe).parent_path();
    // 创建空路径，找到 HLSL 文件后再赋值；空状态也用作“未找到”的标记。
    std::filesystem::path source;
    // 依次尝试进程当前工作目录、exe 所在目录、exe 的上一级目录。
    // const auto & 只读引用当前候选目录；这些路径对应从项目根、构建目录或程序目录启动的情况。
    for (const auto &base : {std::filesystem::current_path(), dir, dir.parent_path()}) {
        // path 的 / 运算符用于拼接路径：候选目录/shaders/file；这里不是除法。
        auto p = base / L"shaders" / file;
        // 检查该候选路径是否存在；存在不保证内容有效或可编译，后面编译器仍会检查。
        if (std::filesystem::exists(p)) {
            // 记录第一个找到的着色器文件完整路径。
            source = p;
            // 停止搜索，前面的候选目录具有更高优先级。
            break;
        }
    }
    // source 仍为空表示三个位置都没找到目标文件。
    if (source.empty())
        // 抛出异常并提示从项目/构建目录运行；字符串过长，分成两个相邻字面量书写。
        throw std::runtime_error("Cannot find shaders. Run from project/build directory or copy "
                                 // 相邻字符串字面量在编译期自动拼接，补充“把 shaders 放到可执行文件旁”的说明。
                                 "shaders beside the executable.");
    // 分别准备接收编译后的字节码和诊断文本；ID3DBlob 是一块带长度的二进制内存。
    ComPtr<ID3DBlob> blob, errors;
    // 调用 HLSL 编译器，先保存 hr，稍后先打印编译诊断，再处理失败。
    HRESULT hr = D3DCompileFromFile(
        // source.c_str() 提供 Windows 宽字符路径；nullptr 表示不额外传入宏定义列表。
        // STANDARD_FILE_INCLUDE 使用标准文件 include 处理器；entry/profile 指定入口和着色器模型。
        source.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, profile,
        // 启用严格语法检查与第 3 级优化；Flags2=0，最后两个参数分别接收字节码和诊断 blob。
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors);
    // 只要编译器产生了诊断缓冲就打印，它可能包含警告，即使最终编译成功也会存在。
    if (errors)
        // 将诊断缓冲 void* 转成 const char*，用 cerr.write 输出原始文本字节。
        std::cerr.write(static_cast<const char *>(errors->GetBufferPointer()),
                        // 按 blob 的实际字节数输出，不依赖诊断缓冲必须以空字符结尾。
                        errors->GetBufferSize());
    // 在输出详细编译日志后检查 hr；失败会抛出带 HRESULT 的异常。
    Check(hr, "D3DCompileFromFile");
    // 返回编译结果的智能指针；局部 errors 自动释放，字节码所有权由返回的 ComPtr 管理。
    return blob;
}
// 把离屏渲染结果读回 CPU，并保存到 path 指定的 PNG 文件；参数是只读路径引用。
// 这是截图路径，包含等待和文件 I/O，不适合放进每帧的性能测量区间。
// 前提：调用线程已经初始化 COM；本项目由 app.cpp 中的 Run 调用 CoInitializeEx。
void Device::Capture(const std::filesystem::path &path) {
    // 准备纹理描述，用于创建与渲染目标尺寸、格式一致的读回副本。
    D3D11_TEXTURE2D_DESC td{};
    // 读取 target 的实际描述，保留其宽高、像素格式、mip 数及采样信息。
    target->GetDesc(&td);
    // 把用途改为 STAGING：用来在 GPU 与 CPU 之间搬运数据，不能直接作为绘制目标使用。
    td.Usage = D3D11_USAGE_STAGING;
    // 清除渲染目标等流水线绑定标志，满足 staging 资源的创建约束。
    td.BindFlags = 0;
    // 允许 CPU 读取这份 staging 纹理；这不表示原来的 target 也能直接被 CPU 读取。
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    // 声明保存读回纹理的智能指针 copy，生命周期限定在本次截图调用中。
    ComPtr<ID3D11Texture2D> copy;
    // 创建读回纹理，不提供初始数据；接下来由 GPU 复制渲染结果填充。
    Check(device->CreateTexture2D(&td, nullptr, &copy), "Capture staging");
    // 排入 target → copy 的整资源复制命令；返回时复制可能还未执行完。
    context->CopyResource(copy.Get(), target.Get());
    // 插入并等待完成事件，确保前面的复制完成；这里显式承担一次 CPU/GPU 同步开销。
    WaitIdle();
    // 清零映射结果结构，用于接收 CPU 地址 pData、每行字节跨度 RowPitch 等信息。
    D3D11_MAPPED_SUBRESOURCE mapped{};
    // 映射 copy 的第 0 个子资源进行 CPU 只读访问；标志 0 不要求非阻塞，结果写入 mapped。
    // 映射范围内必须使用返回的行跨度；完成读取后必须配对调用 Unmap。
    Check(context->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Capture Map");
    // 分配紧密排列的 CPU 像素数组，每像素 BGRA 四个字节。
    // 先转为 size_t 再连乘，让大小计算使用适合内存容量的整数宽度。
    std::vector<BYTE> pixels(static_cast<size_t>(width) * height * 4);
    // 逐行处理所有图像行；这个没有大括号的 for 只控制下一条跨三行书写的 memcpy。
    for (UINT y = 0; y < height; ++y)
        // memcpy 第一个参数是目标行起点：pixels 首地址 + 行号 × 每行有效像素字节数。
        memcpy(pixels.data() + static_cast<size_t>(y) * width * 4,
               // 第二个参数是源行起点：映射首地址 + 行号 × RowPitch。
               // RowPitch 可能大于 width*4，因为驱动可在每行末尾添加对齐填充；不能假设源行紧密排列。
               static_cast<BYTE *>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
               // 第三个参数是本行复制字节数 width*4，只复制实际像素，跳过源纹理的行尾填充。
               width * 4);
    // 结束映射；之后 mapped.pData 不再可用，但已复制到 pixels 的数据仍由 CPU 独立持有。
    context->Unmap(copy.Get(), 0);
    // 如果文件路径包含父目录，才尝试创建目录；仅有文件名时无需创建空目录。
    if (path.has_parent_path())
        // 递归创建缺失的父目录；已有目录不会因此报错，权限等文件系统错误会以异常报告。
        std::filesystem::create_directories(path.parent_path());
    // 准备 WIC 成像工厂接口，它负责创建输出流和图像编码器。
    ComPtr<IWICImagingFactory> factory;
    // 通过 COM 创建 WIC 工厂；CLSID 指明组件类型，nullptr 表示不聚合其他 COM 对象。
    // CLSCTX_INPROC_SERVER 表示使用进程内 DLL 组件，不创建独立服务器进程。
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           // IID_PPV_ARGS 根据 factory 的接口类型提供 IID 和接收接口的指针地址。
                           IID_PPV_ARGS(&factory)),
          // 对创建 WIC 工厂的 HRESULT 做统一检查。
          "WIC factory");
    // 准备 WIC 流接口智能指针，后面将此流绑定到输出文件。
    ComPtr<IWICStream> stream;
    // 由工厂创建一个尚未关联具体文件的 WIC 流对象。
    Check(factory->CreateStream(&stream), "WIC stream");
    // 把流关联到 path 指定的文件，并以 GENERIC_WRITE 请求写入权限；失败由 Check 报告。
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Capture file");
    // 准备图像编码器接口，编码器负责把原始像素组织成某种图像文件格式。
    ComPtr<IWICBitmapEncoder> encoder;
    // GUID_ContainerFormatPng 明确选择 PNG 容器；nullptr 表示不指定特定编码器厂商。
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "PNG encoder");
    // 将编码器连接到输出流，选择 NoCache 模式，避免在此层缓存整份编码输出。
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG init");
    // 准备一帧图像的编码接口；本截图只写一帧。
    ComPtr<IWICBitmapFrameEncode> frame;
    // 为编码器创建新图像帧；第二个 nullptr 表示不接收可选的编码属性包。
    Check(encoder->CreateNewFrame(&frame, nullptr), "PNG frame");
    // 初始化该帧，nullptr 表示使用默认编码选项。
    Check(frame->Initialize(nullptr), "PNG frame init");
    // 设置图像帧的宽高，单位为像素，与 pixels 数组中的图像尺寸一致。
    Check(frame->SetSize(width, height), "PNG size");
    // 请求使用每像素 32 位、BGRA 字节顺序的 WIC 像素格式，与读回缓冲的布局对应。
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    // 向编码器协商像素格式；format 是输入/输出参数，编码器可能改写成另一种支持的格式。
    // 现有实现没有检查返回格式是否仍是 BGRA；更通用的编码流程需要比较并在必要时转换像素。
    Check(frame->SetPixelFormat(&format), "PNG format");
    // 依次传入总行数 height、每行跨度 width*4、缓冲总字节数和像素首地址，把整幅图像交给编码器。
    // 这里是已去掉填充的 CPU 缓冲；转为 UINT 的总大小要求图像字节数在 32 位范围内。
    Check(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), pixels.data()),
          // 检查 WritePixels 的返回码，用 PNG pixels 标识错误发生在写像素阶段。
          "PNG pixels");
    // 提交当前帧，完成该帧的编码；此操作还不是整个 PNG 编码器的最终提交。
    Check(frame->Commit(), "PNG frame commit");
    // 提交编码器，完成图像容器输出；函数退出时各个 ComPtr 自动释放流、帧和编码器引用。
    Check(encoder->Commit(), "PNG commit");
}
// 做基础画面验证：将图像读回，检查有无明显亮像素，并计算一个便于对照的图像哈希。
// 这只能粗略识别“没有可见内容”，不是完整的像素正确性测试，也不比较固定参考哈希。
void Device::ValidateTarget() {
    // 创建临时描述对象，准备用渲染目标的属性创建 staging 纹理。
    D3D11_TEXTURE2D_DESC td{};
    // 复制 target 的描述，确保读回目标与源图像的尺寸、格式和采样数匹配。
    target->GetDesc(&td);
    // 将用途设为 STAGING，以便稍后通过 Map 让 CPU 访问。
    td.Usage = D3D11_USAGE_STAGING;
    // staging 纹理不直接绑定到图形流水线，因此清除绑定标志。
    td.BindFlags = 0;
    // 设置 CPU 只读权限，不申请 CPU 写权限。
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    // 用局部 ComPtr 保存用于验证的 staging 纹理。
    ComPtr<ID3D11Texture2D> staging;
    // 按修改后的描述创建读回纹理；创建失败会终止验证并抛异常。
    Check(device->CreateTexture2D(&td, nullptr, &staging), "Validate image staging");
    // 发出从渲染目标到 staging 纹理的 GPU 复制命令。
    context->CopyResource(staging.Get(), target.Get());
    // 准备接收映射地址和行跨度的结构。
    D3D11_MAPPED_SUBRESOURCE map{};
    // 读取第 0 个子资源，Map 标志为 0，允许等待资源就绪。
    // 这里虽然没有显式 WaitIdle，CPU 仍可能在 Map 内等待 GPU 复制完成；不是异步读回。
    Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Validate image Map");
    // lit 累计亮像素数；hash 初始化为 64 位 FNV-1a 的偏移基数。
    // ull 后缀表示无符号 long long 常量；使用 uint64_t 使后续哈希运算按 64 位进行。
    uint64_t lit = 0, hash = 14695981039346656037ull;
    // 外层循环依次遍历每一行；没有大括号时，其循环体就是紧接着的整个内层 for。
    for (UINT y = 0; y < height; ++y)
        // 内层循环依次遍历该行每个像素，花括号包住这个像素的统计和哈希计算。
        for (UINT x = 0; x < width; ++x) {
            // 把映射数据当作只读 BYTE 字节数组，准备计算当前像素地址。
            auto *pixel = static_cast<const BYTE *>(map.pData) +
                          // 行偏移使用 y*RowPitch，列偏移使用 x*4；这样正确跳过每行可能存在的填充字节。
                          static_cast<size_t>(y) * map.RowPitch + x * 4;
            // 取 B、G、R 三个颜色字节求和，不计 alpha；和大于 100 就视为可见亮像素。
            // 转成 UINT 后做整数加法；这是相对暗背景的经验阈值，不是物理亮度公式。
            if (static_cast<UINT>(pixel[0]) + pixel[1] + pixel[2] > 100)
                // 为满足阈值的像素增加计数；++ 在这里把 lit 加 1。
                ++lit;
            // 遍历当前像素的 B、G、R、A 四个字节，把 alpha 也纳入哈希。
            for (UINT channel = 0; channel < 4; ++channel) {
                // FNV-1a 的第一步：把当前哈希与像素字节做按位异或，^= 表示计算后写回。
                hash ^= pixel[channel];
                // 第二步乘以 FNV 的 64 位素数；无符号溢出按模 2^64 回绕，是算法的一部分。
                hash *= 1099511628211ull;
            }
        }
    // 读取完成后解除映射，不再访问 map.pData。
    context->Unmap(staging.Get(), 0);
    // !lit 等价于 lit==0，判断是否连一个满足阈值的亮像素都没有。
    if (!lit)
        // 若只有背景等暗像素，抛出“没有可见几何图形”的验证失败提示。
        throw std::runtime_error("Rendered image contains no visible geometry");
    // 打印本次基础检查通过、亮像素数和哈希值；不同设备或渲染结果可能产生不同哈希。
    std::cout << "Image PASS, lit pixels=" << lit << " hash=" << hash << '\n';
}
// 为 GPU 计时器预先创建查询对象；gpu 按引用传入，以便访问已初始化的设备。
void GpuTimer::Init(Device &gpu) {
    // 遍历头文件中定义的 16 个槽位；auto & 直接修改槽位，避免复制。
    // 多个槽位允许 CPU 发出后续帧命令时，先前帧的计时结果还在等待 GPU 完成。
    for (auto &s : slots_) {
        // td 描述单个 GPU 时间戳查询；dd 描述一段时间内时钟频率及有效性查询；两者标志均为 0。
        D3D11_QUERY_DESC td{D3D11_QUERY_TIMESTAMP, 0}, dd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        // 为当前槽位创建记录开始位置的时间戳对象，保存到 s.begin。
        Check(gpu.device->CreateQuery(&td, &s.begin), "Timestamp start");
        // 创建记录结束位置的时间戳对象，保存到 s.end。
        Check(gpu.device->CreateQuery(&td, &s.end), "Timestamp end");
        Check(gpu.device->CreateQuery(&td, &s.drawBegin), "Draw timestamp start");
        Check(gpu.device->CreateQuery(&td, &s.drawEnd), "Draw timestamp end");
        // 创建 TIMESTAMP_DISJOINT 查询，用来取得频率并判断这一段的时间戳是否可用于计算。
        Check(gpu.device->CreateQuery(&dd, &s.disjoint), "Timestamp disjoint");
    }
}
// 轮询已提交帧的计时结果；consume 是接收“原始帧号、GPU 毫秒数”的回调函数。
// std::function<void(uint64_t,double)> 可封装 lambda 或函数；此方法在调用线程同步调用它。
void GpuTimer::Poll(Device &gpu, const std::function<void(uint64_t, double)> &consume,
                    const std::function<void(uint64_t, double)> &consumeDraw) {
    // 检查全部槽位；按槽位次序遍历，不保证回调严格按帧号递增，调用方应使用传入的帧号。
    for (auto &s : slots_)
        // 只处理 pending=true 的槽位，它表示已经 End、尚未回收查询结果。
        if (s.pending) {
            // 清零查询结果结构，准备接收 Frequency（每秒计数次数）和 Disjoint（期间是否发生不连续）。
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dd{};
            // 尝试读取当前槽位的 disjoint 查询，输出到 dd；sizeof(dd) 给出输出结构的字节容量。
            HRESULT hr = gpu.context->GetData(s.disjoint.Get(), &dd, sizeof(dd),
                                              // DONOTFLUSH 避免为了取计时结果隐式提交命令；未就绪就留到后续帧再读取。
                                              D3D11_ASYNC_GETDATA_DONOTFLUSH);
            // 真正的失败会抛异常；查询尚未完成时返回 S_FALSE，Check 不会把它当作错误。
            Check(hr, "Poll disjoint");
            // 只有 S_OK 才说明 dd 已可用；其他非失败状态不参与本轮计算。
            if (hr != S_OK)
                // 继续外层槽位循环，当前槽位保留 pending，下次 Poll 再试，而不是在这里忙等 GPU。
                continue;
            // 准备两个 64 位整数来接收开始和结束的 GPU 时钟计数；它们目前都初始化为 0。
            UINT64 a = 0, b = 0;
            // 读取开始时间戳到 a，sizeof(a) 指定 8 字节输出；同样不触发隐式 Flush。
            hr = gpu.context->GetData(s.begin.Get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            // 检查读取开始时间戳是否出现真正错误。
            Check(hr, "Poll begin");
            // 如果开始时间戳还没有准备好，本轮不能计算耗时。
            if (hr != S_OK)
                // 暂时跳过该槽位，保留其待处理状态。
                continue;
            // 读取结束时间戳到 b；它对应 End 中插入的结束标记。
            hr = gpu.context->GetData(s.end.Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            // 检查读取结束时间戳的错误状态。
            Check(hr, "Poll end");
            // 结束时间戳尚未就绪时也不等待。
            if (hr != S_OK)
                // 继续检查其他槽位，当前结果稍后再取。
                continue;
            UINT64 drawA = 0, drawB = 0;
            if (s.drawFinished) {
                hr = gpu.context->GetData(s.drawBegin.Get(), &drawA, sizeof(drawA), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                Check(hr, "Poll draw begin");
                if (hr != S_OK) continue;
                hr = gpu.context->GetData(s.drawEnd.Get(), &drawB, sizeof(drawB), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                Check(hr, "Poll draw end");
                if (hr != S_OK) continue;
                if (!dd.Disjoint && dd.Frequency && drawB >= drawA && consumeDraw)
                    consumeDraw(s.frame, 1000.0 * static_cast<double>(drawB - drawA) / dd.Frequency);
            }
            // 只有区间内时钟连续（Disjoint 为假）且频率非零，才能把计数差换算为有效时间。
            if (!dd.Disjoint && dd.Frequency)
                // 调用结果回调，第一个参数使用当时保存的帧号 s.frame，而不是当前 CPU 帧号。
                consume(s.frame,
                        // b-a 得到 GPU 时钟计数差，除以每秒计数频率得到秒，再乘 1000 转成毫秒。
                        // 显式转为 double 避免整数除法截断；这不是 CPU 提交 API 的耗时。
                        1000.0 * static_cast<double>(b - a) / static_cast<double>(dd.Frequency));
            // 查询数据都已取回，释放此槽位供后续帧复用；即使时钟不连续而丢弃样本，也要回收。
            s.pending = false;
        }
}
// 为 frame 指定的这一帧选一个空闲槽位，并在 GPU 命令流中开始计时区间。
// 调用约定：每帧 Begin 后应配对 End，不能在前一次尚未 End 时再次 Begin。
void GpuTimer::Begin(Device &gpu, uint64_t frame) {
    // 清除本次活动槽指针，随后通过查找空闲槽位来赋值。
    active_ = nullptr;
    // 顺序扫描所有计时槽位。
    for (auto &s : slots_)
        // pending=false 表示该槽位没有等待回收的上一帧查询，可用于本帧。
        if (!s.pending) {
            // 记录这个槽位的地址；它位于固定大小数组中，不会因数组扩容而失效。
            active_ = &s;
            // 找到第一个可用槽位即可，退出查找循环。
            break;
        }
    // 如果所有槽位都在等待 GPU，active_ 仍为空。
    if (!active_) {
        // 累计丢弃的计时样本数；丢的是本帧计时记录，不是画面帧或渲染命令。
        ++dropped;
        // 跳过本帧计时，不阻塞 CPU 等空位；这样测量本身不额外强制同步流水线。
        return;
    }
    // 把本帧编号写入槽位，保证数帧后取结果时仍知道它属于哪一帧。
    active_->frame = frame;
    active_->drawStarted = active_->drawFinished = false;
    // 开始 disjoint 区间，用来验证从此处到 End 之间的时钟是否连续并报告频率。
    gpu.context->Begin(active_->disjoint.Get());
    // 插入开始时间戳。TIMESTAMP 查询只接受 End 来记录单点时间，不调用 Begin。
    // 对象成员名 begin 表示它是“开始位置的时间戳”，并不要求使用同名 D3D API。
    gpu.context->End(active_->begin.Get());
}
void GpuTimer::BeginDraw(Device &gpu) {
    if (!active_) return;
    if (active_->drawStarted) throw std::runtime_error("Draw timer supports one interval per frame");
    active_->drawStarted = true;
    gpu.context->End(active_->drawBegin.Get());
}
void GpuTimer::EndDraw(Device &gpu) {
    if (!active_ || !active_->drawStarted || active_->drawFinished) return;
    gpu.context->End(active_->drawEnd.Get());
    active_->drawFinished = true;
}
// 结束本帧的 GPU 计时区间，将该槽位置为等待回收状态。
void GpuTimer::End(Device &gpu) {
    // Begin 若因没有空槽而跳过，本帧就没有活动槽。
    if (!active_)
        // 直接返回，与被跳过的 Begin 配对，不发出任何查询命令。
        return;
    // 插入结束时间戳，和 active_->begin 中的开始时间戳形成一个区间。
    gpu.context->End(active_->end.Get());
    // 结束 disjoint 查询区间；频率与有效性需等待 GPU 处理后才能通过 GetData 获取。
    gpu.context->End(active_->disjoint.Get());
    // 标记结果待回收，禁止下一帧提前覆盖查询对象；此时并不保证 GPU 已经完成。
    active_->pending = true;
    // 结束当前帧对槽位的占用标记；实际数据仍保留在数组中，稍后由 Poll 查找 pending 槽位。
    active_ = nullptr;
}
// 构造一个固定大小的 CPU 工作线程池；构造函数没有返回类型。
// 每次 Run 让所有 worker 各调用一次 task(worker编号)，由任务自行根据编号划分工作。
// 这不是 GPU 线程池；它不自动保证任务里访问的外部数据或 D3D 上下文线程安全。
Jobs::Jobs(uint32_t count) {
    // 用 std::clamp 把工作线程数限制在 [1,32]；1u 和 32u 是无符号整数，保持模板参数类型一致。
    count = std::clamp(count, 1u, 32u);
    // 为每个编号 i 创建一条线程，编号范围是 0 到 count-1。
    for (uint32_t i = 0; i < count; ++i)
        // emplace_back 直接在 vector 中构造 std::thread，线程从这个 lambda 开始运行。
        // [this,i] 按值捕获对象指针和当前编号；每个线程固定自己的 i，不会引用变化中的循环变量。
        threads_.emplace_back([this, i] {
            // 每个线程有独立的 observed，记录它已经处理过的任务批次；初始为 0。
            uint64_t observed = 0;
            // 工作线程持续循环，完成一批后回到等待点，直到 stop_ 指示退出。
            for (;;) {
                // 取得 mutex_ 互斥锁，保护 stop_、generation_、task_、error_、remaining_ 等共享状态。
                // unique_lock 支持显式解锁/重锁，也能配合条件变量等待；离开作用域时自动解锁。
                std::unique_lock<std::mutex> lock(mutex_);
                // 等待“要求停止”或“出现新批次”；[&] 让谓词按引用读取 observed，并通过 this 访问成员。
                // wait 先检查谓词；需要等待时原子地释放锁并睡眠，唤醒后重新取得锁并再次检查。
                // 因此能处理虚假唤醒，也能处理任务通知早于线程进入 wait 的情况。
                work_.wait(lock, [&] { return stop_ || generation_ != observed; });
                // 在持锁状态检查关闭标志；停止优先于处理新批次。
                if (stop_)
                    // 退出线程 lambda，线程执行结束；局部 unique_lock 随作用域退出而自动释放锁。
                    return;
                // 记录这一批的 generation_，避免同一线程把同一批任务重复执行。
                observed = generation_;
                // 在锁保护下复制 std::function 到线程局部变量，之后可用自己的副本执行任务。
                // 复制 callable 不会复制它引用捕获的外部数据；那些数据的同步仍由任务编写者负责。
                auto task = task_;
                // 执行实际工作前释放线程池状态锁，让其他 worker 能同时取任务并运行。
                lock.unlock();
                // 开始异常捕获区域，防止任务异常直接逃出线程入口而触发 std::terminate。
                try {
                    // 调用本线程的任务副本，并传入固定 worker 编号 i；任务通常据此选择要处理的数据区间。
                    task(i);
                // catch (...) 捕获任务调用抛出的任意 C++ 异常；不能理解成捕获所有操作系统故障。
                // 这里只包围 task(i)，此前复制 std::function 等操作的异常不在这个 catch 范围内。
                } catch (...) {
                    // 异常路径重新取得互斥锁，以便安全写共享 error_；lock_guard 在该 catch 块结束时自动解锁。
                    std::lock_guard<std::mutex> guard(mutex_);
                    // 仅在还没有记录异常时写入，保留首个取得锁并记录下来的任务异常。
                    if (!error_)
                        // 用 exception_ptr 保存当前异常，允许稍后在调用 Run 的线程重新抛出。
                        error_ = std::current_exception();
                }
                // 任务成功或异常记录完成后，再次取得原 unique_lock 的锁，准备更新完成计数。
                lock.lock();
                // 先把剩余 worker 数减 1，再检查是否为 0；互斥锁保证多线程不会同时破坏计数。
                if (--remaining_ == 0)
                    // 最后完成的 worker 唤醒一个等待 done_ 的调用线程；该线程还需重新取得锁并检查谓词。
                    done_.notify_one();
            // 本轮作用域结束，unique_lock 析构解锁；随后开始下一轮，等待下一批任务。
            }
        // 结束线程入口 lambda，并结束 emplace_back 调用；新创建的线程由 threads_ 保存以便最终 join。
        });
}
// 析构函数关闭线程池并回收所有线程，避免 std::thread 仍可 join 时被直接析构。
// 使用前提：对象生命周期由调用方管理，析构不能与新的 Run 并发启动，也不能在本池 worker 中销毁自身。
Jobs::~Jobs() {
    // 人为创建一个较短的作用域，只在设置 stop_ 时持锁，后面的 join 在锁外执行。
    {
        // 以 RAII 方式获取共享状态锁，确保修改 stop_ 时没有数据竞争。
        std::lock_guard<std::mutex> lock(mutex_);
        // 发布停止标志；已在执行的 task 不会被强行打断，worker 回到循环后才检查退出。
        stop_ = true;
    // 局部 lock_guard 离开作用域并解锁，让被唤醒的线程能够重新获取 mutex_。
    }
    // 唤醒所有等待 work_ 的线程，使它们检查 stop_ 并结束。
    work_.notify_all();
    // 按引用遍历所有 std::thread 对象；线程对象不能随意复制。
    for (auto &t : threads_)
        // 等待这条线程退出并回收其线程资源；join 可能阻塞，尤其是仍有 task 正在执行时。
        // 不能持有 worker 退出时需要的 mutex_ 来 join，否则可能互相等待。
        t.join();
}
// 同步执行一批 CPU 工作：所有 worker 各运行一次 task，全部结束后本函数才返回。
// 此处是 Jobs::Run，与 app.cpp 中管理课堂主循环的自由函数 classroom::Run 不是同一个函数。
// 使用约定：一次只允许一个外部调用方派发，且任务不能向同一线程池嵌套调用 Run。
void Jobs::Run(const std::function<void(uint32_t)> &task) {
    // 取得线程池状态锁，在同一临界区内发布本批任务及其计数信息。
    std::unique_lock<std::mutex> lock(mutex_);
    // 复制调用方的 std::function 到成员 task_；任务引用的外部对象需活到所有 worker 完成。
    task_ = task;
    // 把尚未完成的 worker 数设为线程总数，Size() 在头文件中返回 threads_.size()。
    remaining_ = Size();
    // 清空上一批的异常记录，当前批次重新开始收集错误。
    error_ = nullptr;
    // 增加批次号；各 worker 比较 generation_ 和自己的 observed，就能识别新工作。
    ++generation_;
    // 通知全部 worker；通知时仍持锁，所以它们要等下面的 wait 释放锁才能取得任务。
    work_.notify_all();
    // 等待所有 worker 都完成；wait 释放锁让它们运行，返回前再取得锁。
    // [this] 捕获本对象指针，谓词检查 remaining_==0，避免因虚假唤醒而过早返回。
    // 这里会让调用线程等待整批 CPU 工作，与 GPU 的 WaitIdle 是两种不同的同步。
    done_.wait(lock, [this] { return remaining_ == 0; });
    // 所有 worker 完成后检查是否曾有任务抛出异常；此时读取仍在 mutex_ 保护下。
    if (error_)
        // 在调用 Run 的线程重抛保存的异常；栈展开会自动释放 unique_lock，交给上层统一处理。
        std::rethrow_exception(error_);
}
// 关闭 classroom 命名空间；以上工具、设备方法、计时器和线程池实现都属于此命名空间。
} // namespace classroom
