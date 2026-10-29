// Test measurement ownership and render the actual comparison panel with deterministic fixtures.
#include "../src/common/app.cpp"
#include <wincodec.h>
using namespace classroom;

void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Preview(const WindowState &state, const wchar_t *filename) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = ComparisonWidth;
    info.bmiHeader.biHeight = -ClientHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    Require(dc && bitmap && pixels, "Cannot create UI preview surface");
    auto oldBitmap = SelectObject(dc, bitmap);
    HFONT body = CreateFontW(-17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    HFONT title = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                             CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    auto oldFont = SelectObject(dc, body);
    SetBkMode(dc, TRANSPARENT);
    ComparisonPanel(dc, RECT{0, 0, ComparisonWidth, ClientHeight}, state, body, title);
    GdiFlush();
    const auto path = std::filesystem::path(L"../out/validation") / filename;
    std::filesystem::create_directories(path.parent_path());
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&factory)), "Preview WIC");
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream), "Preview stream");
    Check(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "Preview file");
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Preview encoder");
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Preview init");
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(encoder->CreateNewFrame(&frame, nullptr), "Preview frame");
    Check(frame->Initialize(nullptr), "Preview frame init");
    Check(frame->SetSize(ComparisonWidth, ClientHeight), "Preview size");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
    Check(frame->SetPixelFormat(&format), "Preview format");
    ComPtr<IWICBitmap> source;
    Check(factory->CreateBitmapFromMemory(ComparisonWidth, ClientHeight, GUID_WICPixelFormat32bppBGR,
            ComparisonWidth * 4, ComparisonWidth * ClientHeight * 4, static_cast<BYTE *>(pixels), &source),
          "Preview bitmap");
    ComPtr<IWICFormatConverter> converted;
    Check(factory->CreateFormatConverter(&converted), "Preview converter");
    Check(converted->Initialize(source.Get(), format, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom), "Preview conversion");
    Check(frame->WriteSource(converted.Get(), nullptr), "Preview pixels");
    Check(frame->Commit(), "Preview frame commit");
    Check(encoder->Commit(), "Preview commit");
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBitmap);
    DeleteObject(body);
    DeleteObject(title);
    DeleteObject(bitmap);
    DeleteDC(dc);
}
int main() {
    SetProcessDPIAware();
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Test COM");
    Options live;
    WindowState state;
    state.options = &live;
    state.appliedOptions = live;
    state.info.modes = {L"并行组织 + 延后回读", L"串行组织 + 立即回读"};
    state.parameters = {{L"粒子数", &Options::count, 1, 1048576},
                        {L"工作线程", &Options::workers, 1, 32}};
    Preview(state, L"comparison-empty.png");
    RollingMetrics window;
    Metrics m;
    m.cpuMs = 2;
    m.gpuMs = -1;
    window.Add(10, m);
    auto next = live;
    next.workers = 8;
    RequestSettings(state, next);
    Require(state.reset && !state.previous.available, "Snapshot created before frame boundary");
    Require(state.appliedOptions.workers == 4, "Pending settings contaminated sample ownership");
    // A late GPU result must be part of the old configuration, not the new one.
    window.GpuResult(0, 4);
    PreservePrevious(state, window.Get());
    Require(state.previous.available && state.previous.options.workers == 4,
            "Previous settings were overwritten");
    Require(state.previous.averages.mean[RollingMetrics::Gpu] == 4,
            "Late GPU result missing from frozen snapshot");
    window.Clear();
    state.averages = window.Get();
    Require(state.previous.averages.count[RollingMetrics::Cpu] == 1,
            "Clearing current samples changed the snapshot");
    next = live;
    next.count = 10003;
    RequestSettings(state, next);
    PreservePrevious(state, window.Get());
    Require(state.previous.options.workers == 4, "An unsampled adjustment erased the last measurement");
    RequestSettings(state, live);
    Require(!state.reset, "Identical settings caused a reset");
    auto before = state.previous.averages;
    auto after = before;
    after.mean[RollingMetrics::Cpu] = 1;
    Require(MetricChange(before, after, RollingMetrics::Cpu) == L"-50.0%", "Wrong percentage decrease");
    after.mean[RollingMetrics::Cpu] = 3;
    Require(MetricChange(before, after, RollingMetrics::Cpu) == L"+50.0%", "Wrong percentage increase");
    before.mean[RollingMetrics::Cpu] = 0;
    Require(MetricChange(before, after, RollingMetrics::Cpu) == L"+3.000", "Zero baseline divided by zero");
    after.mean[RollingMetrics::Cpu] = 0;
    Require(MetricChange(before, after, RollingMetrics::Cpu) == L"0.0%", "Zero-to-zero comparison incorrect");
    after.count[RollingMetrics::Gpu] = 0;
    Require(MetricChange(before, after, RollingMetrics::Gpu) == L"—" &&
            MetricValue(after, RollingMetrics::Gpu) == L"—", "Missing samples displayed as zero");
    for (int width : {1200, 1366, 1700, 1920}) {
        auto r = ViewportBounds(width, ClientHeight);
        Require(r.left >= 380 && r.right <= width - ComparisonWidth - 16 && r.top >= 32 &&
                r.bottom <= ClientHeight - 44, "Preview overlaps controls or comparison panel");
    }
    // Deterministic preview fixtures, not benchmark measurements.
    state.previous = {Options{}, RollingMetrics::Summary{}, true};
    state.previous.options.mode = 1;
    state.previous.options.workers = 1;
    state.previous.options.vsync = false;
    state.appliedOptions = state.previous.options;
    state.appliedOptions.mode = 0;
    state.appliedOptions.workers = 11;
    state.previous.averages.mean = {4.2, 3.6, 2.8, 0.2, 0.185, 1.13, 2, 0, 0};
    state.averages.mean = {0.728, 3.686, 0.284, 0.21, 0.185, 0.233, 2, 1.894, 0};
    state.previous.averages.count.fill(600);
    state.averages.count.fill(599);
    state.previous.averages.seconds = state.averages.seconds = 10;
    Preview(state, L"comparison-populated-fixture.png");
    state.info.document = "docs/04_cpu_jobs.md";
    state.info.modes = {L"串行更新", L"线程池并行更新"};
    state.previous.averages.mean[RollingMetrics::GpuDraw] = 2.1;
    state.averages.mean[RollingMetrics::GpuDraw] = 2.08;
    Preview(state, L"comparison-draw-timing-fixture.png");
    state.info.document.clear();
    state.parameters.push_back({L"GPU 重复次数", &Options::gpuRepeats, 1, 128});
    state.appliedOptions.count = 1048576;
    state.previous.averages.seconds = 0.8;
    state.averages.seconds = 2.5;
    state.averages.count[RollingMetrics::Gpu] = 0;
    state.paused = true;
    Preview(state, L"comparison-partial-fixture.png");
    CoUninitialize();
    std::cout << "COMPARISON PASS\n";
}
