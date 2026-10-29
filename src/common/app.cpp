#include "classroom.h"
#include "rolling_metrics.h"
namespace classroom {
namespace {
enum ControlId { Mode = 100, Apply, Pause, Vsync, Status, FirstParameter = 200 };
constexpr int ComparisonWidth = 500;
constexpr int MinimumClientWidth = 1200, ClientHeight = 760;
struct Parameter {
    const wchar_t *label;
    uint32_t Options::*member;
    uint32_t minimum, maximum;
    HWND edit = nullptr;
};
struct WindowState {
    LessonInfo info;
    Options *options = nullptr;
    Metrics metrics;
    RollingMetrics::Summary averages;
    // Controls edit options immediately; appliedOptions still names the samples being collected.
    Options appliedOptions;
    struct Snapshot {
        Options options;
        RollingMetrics::Summary averages;
        bool available = false;
    } previous;
    HWND viewport = nullptr;
    std::wstring adapter;
    bool reset = false, paused = false;
    uint64_t shown = 0;
    std::vector<Parameter> parameters;
};
bool SameSettings(const Options &a, const Options &b) {
    return a.mode == b.mode && a.count == b.count && a.workers == b.workers && a.draws == b.draws &&
           a.cpuMs == b.cpuMs && a.gpuRepeats == b.gpuRepeats && a.vsync == b.vsync;
}
void RequestSettings(WindowState &state, const Options &next) {
    *state.options = next;
    state.reset = !SameSettings(state.appliedOptions, next);
}
void PreservePrevious(WindowState &state, const RollingMetrics::Summary &completed) {
    if (SameSettings(state.appliedOptions, *state.options))
        return;
    // A paused or rapidly replaced configuration may have no samples. Keep the last measured run.
    if (completed.count[RollingMetrics::Cpu])
        state.previous = {state.appliedOptions, completed, true};
    state.appliedOptions = *state.options;
}
RECT ViewportBounds(int clientWidth, int clientHeight) {
    const int availableWidth = std::max(1, clientWidth - ComparisonWidth - 396);
    const int availableHeight = std::max(1, clientHeight - 76);
    const double scale = std::min({1.0, availableWidth / 800.0, availableHeight / 640.0});
    const int width = std::max(1, static_cast<int>(800 * scale));
    const int height = std::max(1, static_cast<int>(640 * scale));
    const int x = 380 + (availableWidth - width) / 2;
    const int y = 32 + (availableHeight - height) / 2;
    return {x, y, x + width, y + height};
}
void LayoutViewport(HWND window, WindowState &state) {
    if (!state.viewport)
        return;
    RECT client{};
    GetClientRect(window, &client);
    const auto r = ViewportBounds(client.right, client.bottom);
    // Only scale the displayed preview. Keep the measured DX11 render target at 800 x 640.
    MoveWindow(state.viewport, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
}
HWND Control(HWND parent, const wchar_t *kind, const wchar_t *text, DWORD style, int id, int x,
             int y, int width, int height) {
    HWND child = CreateWindowW(kind, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                               parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                               GetModuleHandleW(nullptr), nullptr);
    if (!child)
        throw std::runtime_error("Cannot create parameter control");
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
    return child;
}
void ApplyParameters(HWND window, WindowState &state) {
    KillTimer(window, 1);
    Options next = *state.options;
    for (const auto &p : state.parameters) {
        wchar_t text[64]{};
        GetWindowTextW(p.edit, text, 64);
        wchar_t *end = nullptr;
        const auto value = wcstoull(text, &end, 10);
        if (!*text || *end || value < p.minimum || value > p.maximum) {
            std::wstring error = std::wstring(p.label) + L"范围：" + std::to_wstring(p.minimum) +
                                 L"–" + std::to_wstring(p.maximum) + L"；尚未应用";
            SetDlgItemTextW(window, Status, error.c_str());
            return;
        }
        next.*(p.member) = static_cast<uint32_t>(value);
    }
    bool changed = false;
    for (const auto &p : state.parameters)
        changed |= next.*(p.member) != state.options->*(p.member);
    if (changed) {
        RequestSettings(state, next);
        SetDlgItemTextW(window, Status, L"正在应用参数…");
    } else
        SetDlgItemTextW(window, Status, L"参数已生效；修改数值后自动应用");
}
void CreateControls(HWND window, WindowState &state) {
    const auto chapter = state.info.document.substr(5, 2);
    if (chapter != "07")
        state.parameters.push_back({L"粒子数", &Options::count, 1, 1048576});
    if (chapter == "04" || chapter == "07" || chapter == "08")
        state.parameters.push_back({L"工作线程", &Options::workers, 1, 32});
    if (chapter == "07")
        state.parameters.push_back({L"Draw 次数", &Options::draws, 1, 65536});
    if (chapter == "01")
        state.parameters.push_back({L"CPU 负载 (ms)", &Options::cpuMs, 0, 100});
    if (chapter == "01" || chapter == "02")
        state.parameters.push_back({L"GPU 重复次数", &Options::gpuRepeats, 1, 128});
    auto combo = Control(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, Mode,
                         22, 350, 338, 160);
    for (const auto &mode : state.info.modes)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mode.c_str()));
    SendMessageW(combo, CB_SETCURSEL, state.options->mode, 0);
    int row = 0;
    for (auto &p : state.parameters) {
        Control(window, L"STATIC", p.label, 0, -1, 22, 390 + row * 32, 148, 24);
        p.edit = Control(window, L"EDIT", std::to_wstring(state.options->*(p.member)).c_str(),
                         WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, FirstParameter + row,
                         174, 386 + row * 32, 186, 27);
        SendMessageW(p.edit, EM_SETLIMITTEXT, 10, 0);
        ++row;
    }
    Control(window, L"BUTTON", L"立即应用", WS_TABSTOP, Apply, 22, 490, 102, 29);
    Control(window, L"BUTTON", L"暂停", WS_TABSTOP, Pause, 132, 490, 80, 29);
    Control(window, L"BUTTON", L"垂直同步", WS_TABSTOP | BS_AUTOCHECKBOX, Vsync, 224, 490, 136, 29);
    CheckDlgButton(window, Vsync, state.options->vsync ? BST_CHECKED : BST_UNCHECKED);
    Control(window, L"STATIC", L"修改后自动应用；重置模拟与统计", 0, Status, 22, 524, 338, 26);
    KillTimer(window, 1);
}
void Text(HDC dc, const std::wstring &s, int x, int y, int width, int height, COLORREF color,
          HFONT font) {
    SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT r{x, y, x + width, y + height};
    DrawTextW(dc, s.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
}
std::wstring Number(double value, int precision = 3) {
    std::wostringstream text;
    text << std::fixed << std::setprecision(precision) << value;
    return text.str();
}
std::wstring MetricValue(const RollingMetrics::Summary &summary, RollingMetrics::Field field) {
    return summary.count[field] ? Number(summary.mean[field]) : L"—";
}
std::wstring MetricChange(const RollingMetrics::Summary &before,
                          const RollingMetrics::Summary &after, RollingMetrics::Field field) {
    if (!before.count[field] || !after.count[field])
        return L"—";
    const double delta = after.mean[field] - before.mean[field];
    const auto sign = delta > 0 ? L"+" : L"";
    if (std::abs(before.mean[field]) < 0.000000001)
        return std::abs(delta) < 0.000000001 ? L"0.0%" : sign + Number(delta);
    return sign + Number(100.0 * delta / before.mean[field], 1) + L"%";
}
void ComparisonPanel(HDC dc, const RECT &panel, const WindowState &state, HFONT body, HFONT title) {
    const COLORREF muted = RGB(157, 176, 202), normal = RGB(226, 234, 247);
    const COLORREF accent = RGB(102, 211, 251), better = RGB(112, 226, 173),
                   worse = RGB(255, 155, 148);
    SetDCBrushColor(dc, RGB(18, 27, 43));
    FillRect(dc, &panel, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const int x = panel.left + 18;
    Text(dc, L"参数与性能对比", x, 22, 464, 34, normal, title);
    const auto &current = state.averages;
    const auto &previous = state.previous.averages;
    const bool hasPrevious = state.previous.available;
    std::wstring sampling = state.paused ? L"已暂停"
                            : current.seconds >= 10
                                ? L"当前已采满 10 秒"
                                : L"当前采集中 " + Number(current.seconds, 1) + L" / 10s";
    Text(dc, sampling + L" · 修改设置时自动保留上次", x, 59, 464, 24, accent, body);
    Text(dc, L"上次模式 · 已冻结", x, 94, 222, 24, muted, body);
    Text(dc, state.paused ? L"当前模式 · 已暂停" : L"当前模式 · 实时", x + 240, 94, 224, 24, accent,
         body);
    Text(dc,
         hasPrevious ? state.info.modes[state.previous.options.mode]
                     : L"尚无记录，调整一次参数后显示",
         x, 122, 222, 52, normal, body);
    Text(dc, state.info.modes[state.appliedOptions.mode], x + 240, 122, 224, 52, normal, body);

    // Shared column positions keep settings and measured values aligned vertically.
    auto row = [&](int y, const std::wstring &label, const std::wstring &oldValue,
                   const std::wstring &newValue, const std::wstring &change, COLORREF color) {
        Text(dc, label, x, y, 134, 25, muted, body);
        Text(dc, oldValue, x + 138, y, 98, 25, normal, body);
        Text(dc, newValue, x + 244, y, 98, 25, accent, body);
        Text(dc, change, x + 350, y, 114, 25, color, body);
    };
    row(184, L"设置", L"上次", L"当前", L"变化", muted);
    int y = 216;
    for (const auto &p : state.parameters) {
        const auto now = state.appliedOptions.*(p.member);
        const auto old = state.previous.options.*(p.member);
        row(y, p.label, hasPrevious ? std::to_wstring(old) : L"—", std::to_wstring(now),
            !hasPrevious ? L"—"
            : old == now ? L"相同"
                         : L"已调整",
            old == now ? muted : accent);
        y += 28;
    }
    row(y, L"垂直同步", hasPrevious ? (state.previous.options.vsync ? L"开" : L"关") : L"—",
        state.appliedOptions.vsync ? L"开" : L"关",
        !hasPrevious                                                 ? L"—"
        : state.previous.options.vsync == state.appliedOptions.vsync ? L"相同"
                                                                     : L"已调整",
        accent);
    y += 42;
    row(y, L"近 10s 均值", L"上次", L"当前", L"相对上次", muted);
    y += 32;
    const wchar_t *labels[] = {L"CPU 提交 ms",   L"GPU 帧区间 ms", L"CPU 更新 ms", L"呈现 ms",
                               L"上传 ms",       L"回读 ms",       L"Draw / 帧",   L"回读帧龄",
                               L"回读丢样 / 帧", L"GPU 绘制 ms"};
    const RollingMetrics::Field order[] = {RollingMetrics::Cpu,      RollingMetrics::Gpu,
                                           RollingMetrics::GpuDraw,  RollingMetrics::Update,
                                           RollingMetrics::Present,  RollingMetrics::Upload,
                                           RollingMetrics::Readback, RollingMetrics::Draws,
                                           RollingMetrics::Age,      RollingMetrics::Drops};
    for (int i = 0; i < RollingMetrics::FieldCount; ++i) {
        const auto field = order[i];
        if (field == RollingMetrics::GpuDraw && state.info.document != "docs/04_cpu_jobs.md")
            continue;
        if (i % 2 == 0) {
            RECT stripe{panel.left + 10, y - 2, panel.right - 10, y + 23};
            SetDCBrushColor(dc, RGB(24, 36, 55));
            FillRect(dc, &stripe, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        const double delta = current.mean[field] - previous.mean[field];
        const bool valid = hasPrevious && current.count[field] && previous.count[field];
        const auto color = !valid || field == RollingMetrics::Draws || std::abs(delta) < 0.000000001
                               ? muted
                           : delta < 0 ? better
                                       : worse;
        row(y, labels[field], hasPrevious ? MetricValue(previous, field) : L"—",
            MetricValue(current, field),
            hasPrevious ? MetricChange(previous, current, field) : L"—", color);
        y += 24;
    }
    y += 8;
    row(y, L"采样时长 s", hasPrevious ? Number(previous.seconds, 1) : L"—",
        Number(current.seconds, 1), L"", muted);
    y += 24;
    row(y, L"CPU 样本", hasPrevious ? std::to_wstring(previous.count[RollingMetrics::Cpu]) : L"—",
        std::to_wstring(current.count[RollingMetrics::Cpu]), L"", muted);
    y += 24;
    row(y, L"GPU 样本 帧/绘",
        hasPrevious ? std::to_wstring(previous.count[RollingMetrics::Gpu]) + L"/" +
                          std::to_wstring(previous.count[RollingMetrics::GpuDraw])
                    : L"—",
        std::to_wstring(current.count[RollingMetrics::Gpu]) + L"/" +
            std::to_wstring(current.count[RollingMetrics::GpuDraw]),
        L"", muted);
    y += 30;
    std::wstring note = !hasPrevious ? L"先运行一组参数，再调整；旧设置与均值会自动保留。"
                        : previous.seconds < 10 || current.seconds < 10
                            ? L"有一组未采满 10 秒，当前差异仅供初步观察。"
                            : L"绿色减少 / 红色增加；耗时越低越好，波动需重复验证。";
    Text(dc,
         note + (state.appliedOptions.vsync ? L"\nVSync 开启：呈现可能等待刷新；GPU 数值非利用率。"
                                            : L"\nGPU 帧区间可能含供给空隙；绘制也非纯忙碌时间。"),
         x, y, 464, 52, muted, body);
}
LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto *state = reinterpret_cast<WindowState *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<WindowState *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_GETMINMAXINFO) {
        RECT minimum{0, 0, MinimumClientWidth, ClientHeight};
        AdjustWindowRect(&minimum, static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE);
        auto *limits = reinterpret_cast<MINMAXINFO *>(l);
        limits->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        return 0;
    }
    if (message == WM_SIZE && state) {
        LayoutViewport(hwnd, *state);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_KEYDOWN && state) {
        if (w == VK_ESCAPE)
            DestroyWindow(hwnd);
        if (w == 'M') {
            Options next = *state->options;
            next.mode =
                (state->options->mode + 1) % static_cast<uint32_t>(state->info.modes.size());
            RequestSettings(*state, next);
            SendDlgItemMessageW(hwnd, Mode, CB_SETCURSEL, state->options->mode, 0);
        }
        if (w == 'V') {
            Options next = *state->options;
            next.vsync = !next.vsync;
            RequestSettings(*state, next);
            CheckDlgButton(hwnd, Vsync, state->options->vsync ? BST_CHECKED : BST_UNCHECKED);
        }
        if (w == VK_SPACE) {
            state->paused = !state->paused;
            SetDlgItemTextW(hwnd, Pause, state->paused ? L"继续" : L"暂停");
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_COMMAND && state) {
        const int id = LOWORD(w), notification = HIWORD(w);
        if (id >= FirstParameter && notification == EN_CHANGE) {
            SetDlgItemTextW(hwnd, Status, L"等待输入完成（0.6 秒后自动应用）");
            SetTimer(hwnd, 1, 600, nullptr);
        } else if (id == Apply && notification == BN_CLICKED)
            ApplyParameters(hwnd, *state);
        else if (id == Mode && notification == CBN_SELCHANGE) {
            Options next = *state->options;
            next.mode = static_cast<uint32_t>(SendDlgItemMessageW(hwnd, Mode, CB_GETCURSEL, 0, 0));
            RequestSettings(*state, next);
        } else if (id == Pause && notification == BN_CLICKED) {
            state->paused = !state->paused;
            SetDlgItemTextW(hwnd, Pause, state->paused ? L"继续" : L"暂停");
        } else if (id == Vsync && notification == BN_CLICKED) {
            Options next = *state->options;
            next.vsync = IsDlgButtonChecked(hwnd, Vsync) == BST_CHECKED;
            RequestSettings(*state, next);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (message == WM_TIMER && w == 1 && state) {
        ApplyParameters(hwnd, *state);
        return 0;
    }
    if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN) {
        HDC dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, RGB(223, 232, 246));
        SetBkColor(dc, RGB(13, 19, 32));
        SetDCBrushColor(dc, RGB(13, 19, 32));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    if (message == WM_PAINT && state) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        HBRUSH bg = CreateSolidBrush(RGB(13, 19, 32));
        FillRect(dc, &client, bg);
        DeleteObject(bg);
        SetBkMode(dc, TRANSPARENT);
        HFONT body = CreateFontW(-17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                 CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        HFONT title = CreateFontW(-25, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                                  CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        HGDIOBJ old = SelectObject(dc, body);
        Text(dc, L"DX11 / 并行实验室", 22, 22, 340, 38, RGB(91, 204, 245), body);
        Text(dc, state->info.title, 22, 60, 340, 72, RGB(242, 246, 255), title);
        Text(dc, L"本章目标\n" + state->info.goal, 22, 141, 340, 83, RGB(223, 232, 246), body);
        Text(dc, L"学习内容\n" + state->info.concepts, 22, 232, 340, 108, RGB(167, 185, 209), body);
        std::wostringstream values;
        const auto &a = state->averages;
        auto value = [&](RollingMetrics::Field field) {
            if (!a.count[field])
                return std::wstring(L"—");
            std::wostringstream text;
            text << std::fixed << std::setprecision(3) << a.mean[field];
            return text.str();
        };
        values << std::fixed << std::setprecision(1) << L"近 10s 均值 · 已采集 " << a.seconds
               << L"s\n"
               << L"CPU / GPU帧：" << value(RollingMetrics::Cpu) << L" / "
               << value(RollingMetrics::Gpu) << L" ms\n"
               << L"更新 / 呈现：" << value(RollingMetrics::Update) << L" / "
               << value(RollingMetrics::Present) << L" ms\n"
               << L"上传 / 回读：" << value(RollingMetrics::Upload) << L" / "
               << value(RollingMetrics::Readback) << L" ms\n"
               << L"Draw：" << value(RollingMetrics::Draws) << L"  帧龄："
               << value(RollingMetrics::Age) << L"\n"
               << L"回读丢样：" << value(RollingMetrics::Drops) << L" 次/帧\n"
               << L"有效样本 CPU / GPU：" << a.count[RollingMetrics::Cpu] << L" / "
               << a.count[RollingMetrics::Gpu];
        if (state->info.document == "docs/04_cpu_jobs.md")
            values << L"\nGPU 绘制：" << value(RollingMetrics::GpuDraw) << L" ms";
        Text(dc, values.str(), 22, 550, 345, 194, RGB(223, 232, 246), body);
        const int previewWidth = client.right - ComparisonWidth - 396;
        Text(dc, L"SPACE 暂停 · V 垂直同步 · ESC 退出", 380, client.bottom - 35, previewWidth, 28,
             RGB(167, 185, 209), body);
        Text(dc,
             L"GPU: " + state->adapter + (state->paused ? L"   [暂停]" : L"") +
                 (state->options->vsync ? L"   VSync ON" : L"   VSync OFF"),
             380, 4, previewWidth, 24, RGB(167, 185, 209), body);
        ComparisonPanel(dc, RECT{client.right - ComparisonWidth, 0, client.right, client.bottom},
                        *state, body, title);
        SelectObject(dc, old);
        DeleteObject(title);
        DeleteObject(body);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
Options Parse(int argc, char **argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        auto value = [&] {
            if (i + 1 >= argc)
                throw std::runtime_error("Missing value for " + key);
            return std::string(argv[++i]);
        };
        auto number = [&] {
            auto s = value();
            size_t end = 0;
            auto v = std::stoull(s, &end);
            if (end != s.size() || v > 10000000)
                throw std::runtime_error("Invalid number for " + key);
            return static_cast<uint32_t>(v);
        };
        if (key == "--headless")
            o.headless = true;
        else if (key == "--warp")
            o.warp = true;
        else if (key == "--debug")
            o.debug = true;
        else if (key == "--validate")
            o.validate = true;
        else if (key == "--no-vsync")
            o.vsync = false;
        else if (key == "--frames")
            o.frames = number();
        else if (key == "--count")
            o.count = number();
        else if (key == "--draws")
            o.draws = number();
        else if (key == "--workers")
            o.workers = number();
        else if (key == "--mode")
            o.mode = number();
        else if (key == "--cpu-ms")
            o.cpuMs = number();
        else if (key == "--gpu-repeat")
            o.gpuRepeats = number();
        else if (key == "--output")
            o.output = std::filesystem::u8path(value());
        else if (key == "--capture")
            o.capture = std::filesystem::u8path(value());
        else
            throw std::runtime_error("Unknown option: " + key);
    }
    if (o.count < 1 || o.count > 1048576 || o.draws < 1 || o.draws > 65536 || o.workers < 1 ||
        o.workers > 32 || o.cpuMs > 100 || o.gpuRepeats < 1 || o.gpuRepeats > 128)
        throw std::runtime_error("Range: count 1..1048576, draws 1..65536, workers 1..32, cpu-ms "
                                 "0..100, gpu-repeat 1..128");
    if (o.headless && !o.frames)
        o.frames = 120;
    return o;
}
double Percentile(std::vector<double> values, double p) {
    if (values.empty())
        return -1;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>((values.size() - 1) * p)];
}
void WriteResults(const Options &o, const Device &gpu, const std::vector<Metrics> &samples,
                  uint32_t errors, uint32_t timerDrops, bool validated) {
    if (o.output.empty())
        return;
    if (o.output.has_parent_path())
        std::filesystem::create_directories(o.output.parent_path());
    std::ofstream file(o.output);
    if (!file)
        throw std::runtime_error("Cannot open CSV output");
    file << "frame,cpu_submit_ms,gpu_interval_ms,gpu_source_frame,present_ms,update_ms,upload_ms,"
            "readback_ms,draw_calls,readback_age,dropped_readbacks,gpu_draw_ms\n"
         << std::fixed << std::setprecision(6);
    std::vector<double> cpu, gpuTimes, frameTimes, drawTimes;
    const size_t warmup = std::min<size_t>(30, samples.size() / 4);
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto &s = samples[i];
        file << s.frame << ',' << s.cpuMs << ',' << s.gpuMs << ',' << s.gpuFrame << ','
             << s.presentMs << ',' << s.updateMs << ',' << s.uploadMs << ',' << s.readbackMs << ','
             << s.drawCalls << ',' << s.readbackAge << ',' << s.droppedReadbacks << ','
             << s.gpuDrawMs << '\n';
        if (i >= warmup) {
            cpu.push_back(s.cpuMs);
            frameTimes.push_back(s.cpuMs + s.presentMs);
            if (s.gpuMs >= 0)
                gpuTimes.push_back(s.gpuMs);
            if (s.gpuDrawMs >= 0)
                drawTimes.push_back(s.gpuDrawMs);
        }
    }
    auto summary = o.output;
    summary.replace_extension(".json");
    std::ofstream json(summary);
    if (!json)
        throw std::runtime_error("Cannot open JSON summary");
    // Adapter names from DXGI are ordinary strings; escape quotes/backslashes for valid JSON.
    std::string name;
    for (char c : Utf8(gpu.adapter)) {
        if (c == '"' || c == '\\')
            name += '\\';
        name += c;
    }
    json << std::fixed << std::setprecision(6) << "{\n  \"adapter\": \"" << name
         << "\",\n  \"frames\": " << samples.size() << ",\n  \"mode\": " << o.mode
         << ",\n  \"count\": " << o.count << ",\n  \"workers\": " << o.workers
         << ",\n  \"warmup_frames\": " << warmup
         << ",\n  \"cpu_submit_median_ms\": " << Percentile(cpu, 0.5)
         << ",\n  \"gpu_interval_median_ms\": " << Percentile(gpuTimes, 0.5)
         << ",\n  \"cpu_frame_p95_ms\": " << Percentile(frameTimes, 0.95)
         << ",\n  \"gpu_samples\": " << gpuTimes.size()
         << ",\n  \"gpu_draw_samples\": " << drawTimes.size()
         << ",\n  \"gpu_draw_median_ms\": " << Percentile(drawTimes, 0.5)
         << ",\n  \"timer_dropped_frames\": " << timerDrops
         << ",\n  \"debug_layer\": " << (gpu.debugActive ? "true" : "false")
         << ",\n  \"debug_warnings_or_errors\": " << errors
         << ",\n  \"validated\": " << (validated ? "true" : "false") << "\n}\n";
}
} // namespace
int Run(int argc, char **argv, std::unique_ptr<Lesson> lesson) {
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDPIAware();
    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HWND window = nullptr;
    try {
        Options o = Parse(argc, argv);
        LessonInfo info = lesson->Info();
        if (o.mode >= info.modes.size())
            throw std::runtime_error("Mode out of range for chapter");
        std::cout << Utf8(info.title) << "\n"
                  << Utf8(info.goal) << "\nMode: " << Utf8(info.modes[o.mode])
                  << "\nRead: " << info.document << "\n";
        WindowState state{info, &o};
        state.appliedOptions = o;
        HWND viewport = nullptr;
        if (!o.headless) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = Procedure;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"DX11Classroom";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            RegisterClassW(&wc);
            MONITORINFO monitor{sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
            const int clientWidth = std::max(
                MinimumClientWidth,
                std::min(1700, static_cast<int>(monitor.rcWork.right - monitor.rcWork.left) - 40));
            RECT rect{0, 0, clientWidth, ClientHeight};
            DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
            AdjustWindowRect(&rect, style, FALSE);
            window = CreateWindowW(wc.lpszClassName, info.title.c_str(), style, CW_USEDEFAULT,
                                   CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                   nullptr, nullptr, wc.hInstance, &state);
            if (!window)
                throw std::runtime_error("CreateWindow failed");
            viewport = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 380, 28, 800, 640,
                                     window, nullptr, wc.hInstance, nullptr);
            if (!viewport)
                throw std::runtime_error("Create viewport failed");
            state.viewport = viewport;
            LayoutViewport(window, state);
            CreateControls(window, state);
        }
        Device gpu;
        gpu.Init(viewport, o);
        state.adapter = gpu.adapter;
        std::cout << "Adapter: " << Utf8(gpu.adapter) << "\nDebug layer: " << gpu.debugActive
                  << '\n';
        GpuTimer timer;
        timer.Init(gpu);
        if (info.document == "docs/04_cpu_jobs.md")
            gpu.drawTimer = &timer;
        lesson->Init(gpu, o);
        if (window) {
            ShowWindow(window, SW_SHOW);
            UpdateWindow(window);
        }
        std::vector<Metrics> samples;
        uint64_t simulated = 0;
        uint64_t totalFrames = 0;
        bool running = true;
        auto lastUi = Clock::now();
        double latestGpu = -1;
        int64_t latestGpuFrame = -1;
        RollingMetrics rolling;
        double activeSeconds = 0;
        auto lastTick = Clock::now();
        bool wasPaused = false;
        auto consumeDraw = [&](uint64_t frame, double ms) {
            rolling.GpuResult(frame, ms, RollingMetrics::GpuDraw);
            if (frame < samples.size())
                samples[frame].gpuDrawMs = ms;
        };
        auto consume = [&](uint64_t frame, double ms) {
            rolling.GpuResult(frame, ms);
            if (frame < samples.size()) {
                samples[frame].gpuMs = ms;
                samples[frame].gpuFrame = static_cast<int64_t>(frame);
            }
            if (static_cast<int64_t>(frame) > latestGpuFrame) {
                latestGpu = ms;
                latestGpuFrame = static_cast<int64_t>(frame);
            }
        };
        while (running && (!o.frames || totalFrames < o.frames)) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT)
                    running = false;
                if (window && IsDialogMessageW(window, &msg))
                    continue;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running)
                break;
            if (state.reset) {
                gpu.WaitIdle();
                timer.Poll(gpu, consume, consumeDraw);
                // Drain late GPU timestamps before freezing the OLD applied settings and samples.
                PreservePrevious(state, rolling.Get());
                gpu.context->ClearState();
                lesson->Init(gpu, o);
                // A new parameter set starts a new measurement segment. Never mix old samples
                // or delayed timestamp results into the new configuration's report.
                samples.clear();
                latestGpu = -1;
                latestGpuFrame = -1;
                timer.dropped = 0;
                state.metrics = Metrics{};
                rolling.Clear();
                state.averages = rolling.Get();
                activeSeconds = 0;
                lastTick = Clock::now();
                simulated = 0;
                state.reset = false;
                if (window) {
                    SetDlgItemTextW(window, Status,
                                    state.paused ? L"已重置；点击继续显示新画面"
                                                 : L"已应用；模拟与统计从新参数开始");
                    InvalidateRect(window, nullptr, FALSE);
                }
            }
            const auto tick = Clock::now();
            if (!state.paused && !wasPaused)
                activeSeconds += std::chrono::duration<double>(tick - lastTick).count();
            lastTick = tick;
            wasPaused = state.paused;
            if (state.paused) {
                Sleep(16);
                continue;
            }
            timer.Poll(gpu, consume, consumeDraw);
            Metrics m;
            m.frame = samples.size();
            const auto begin = Clock::now();
            gpu.Begin();
            timer.Begin(gpu, m.frame);
            lesson->Frame(gpu, o, m, 1.0f / 60.0f);
            ++simulated;
            ++totalFrames;
            timer.End(gpu);
            m.cpuMs = Ms(begin);
            auto present = Clock::now();
            gpu.Present(o.vsync);
            m.presentMs = Ms(present);
            samples.push_back(m);
            rolling.Add(activeSeconds, m);
            if (window && Ms(lastUi) > 150) {
                state.metrics = m;
                state.metrics.gpuMs = latestGpu;
                state.metrics.gpuFrame = latestGpuFrame;
                state.averages = rolling.Get();
                InvalidateRect(window, nullptr, FALSE);
                lastUi = Clock::now();
            }
        }
        gpu.WaitIdle();
        timer.Poll(gpu, consume, consumeDraw);
        const bool validated = o.validate && simulated > 0;
        if (validated) {
            if (gpu.drawTimer) {
                size_t validDraws = 0;
                for (const auto &s : samples)
                    if (s.gpuMs >= 0) {
                        if (s.gpuDrawMs < 0 || s.gpuDrawMs > s.gpuMs + 0.000001)
                            throw std::runtime_error(
                                "Draw interval missing or outside frame interval");
                        ++validDraws;
                    }
                if (!validDraws)
                    throw std::runtime_error("No valid draw timing samples");
            }
            lesson->Validate(gpu, o, simulated);
            if (samples.empty())
                throw std::runtime_error("No frames rendered");
            gpu.ValidateTarget();
        }
        if (!o.capture.empty())
            gpu.Capture(o.capture);
        uint32_t errors = gpu.DebugErrors();
        WriteResults(o, gpu, samples, errors, timer.dropped, validated && errors == 0);
        std::cout << "Frames=" << samples.size() << " GPU timer dropped=" << timer.dropped
                  << " Debug warnings/errors=" << errors << '\n';
        if (errors)
            throw std::runtime_error("D3D11 debug layer reported warnings/errors");
        std::cout << (validated ? "VALIDATION PASS\n" : "RUN COMPLETE\n");
        gpu.context->ClearState();
        lesson.reset();
        if (window && IsWindow(window))
            DestroyWindow(window);
        if (SUCCEEDED(com))
            CoUninitialize();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        if (window && IsWindow(window))
            DestroyWindow(window);
        if (SUCCEEDED(com))
            CoUninitialize();
        return 1;
    }
}
} // namespace classroom
