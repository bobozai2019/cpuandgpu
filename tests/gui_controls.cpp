// In-process integration test: exercise the actual native controls and frame-boundary reset.
#include "../src/common/app.cpp"
#define main pipeline_entry
#include "../src/chapters/01_pipeline.cpp"
#undef main
#define main timing_entry
#include "../src/chapters/02_timing.cpp"
#undef main
#define main upload_entry
#include "../src/chapters/03_upload.cpp"
#undef main
#define main jobs_entry
#include "../src/chapters/04_cpu_jobs.cpp"
#undef main
#define main compute_entry
#include "../src/chapters/05_compute.cpp"
#undef main
#define main readback_entry
#include "../src/chapters/06_readback.cpp"
#undef main
#define main deferred_entry
#include "../src/chapters/07_deferred.cpp"
#undef main
#define main integrated_entry
#include "../src/chapters/08_integrated.cpp"
#undef main

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
HWND OwnWindow() {
    HWND result = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
        wchar_t name[64]{};
        GetClassNameW(window, name, 64);
        if (std::wstring(name) == L"DX11Classroom") {
            *reinterpret_cast<HWND *>(data) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    Require(result != nullptr, "No window on test thread");
    return result;
}
class Exercise : public Lesson {
    std::unique_ptr<Lesson> inner;
    int initializations = 0, frames = 0;
  public:
    explicit Exercise(std::unique_ptr<Lesson> lesson) : inner(std::move(lesson)) {}
    LessonInfo Info() const override { return inner->Info(); }
    void Init(Device &gpu, const Options &o) override {
        inner->Init(gpu, o);
        ++initializations;
        frames = 0;
        if (initializations == 2) {
            Require(o.mode == 1, "Mode not applied at frame boundary");
            if (Info().document != "docs/07_deferred.md")
                Require(o.count == 10003, "Particle count not applied");
        }
        if (initializations >= 2) {
            auto &state = *reinterpret_cast<classroom::WindowState *>(
                GetWindowLongPtrW(OwnWindow(), GWLP_USERDATA));
            Require(state.previous.available, "Previous measurement was not preserved");
            Require(state.previous.options.mode == (initializations == 2 ? 0u : 1u),
                    "Snapshot was labeled with the new settings instead of the old settings");
            Require(state.previous.averages.count[RollingMetrics::Cpu] == 5,
                    "Snapshot did not include all frames before reset");
            Require(state.previous.averages.count[RollingMetrics::Gpu] > 0,
                    "Previous GPU timestamps were not drained before snapshot");
            Require(SameSettings(state.appliedOptions, o), "Displayed applied settings are stale");
            Require(gpu.width == 800 && gpu.height == 640, "UI layout changed measured render resolution");
        }
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float dt) override {
        inner->Frame(gpu, o, m, dt);
        if (++frames != 5 || initializations > 2) return;
        HWND window = OwnWindow();
        auto &state = *reinterpret_cast<classroom::WindowState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (initializations == 1) {
            SendMessageW(window, WM_KEYDOWN, 'V', 0);
            Require(state.reset && o.vsync, "VSync change did not request a snapshot boundary");
            SendMessageW(window, WM_KEYDOWN, 'V', 0);
            Require(!state.reset && !o.vsync, "Returning to the applied settings must cancel reset");
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(classroom::Pause, BN_CLICKED), 0);
            Require(state.paused, "Pause button failed");
            auto &first = state.parameters.front();
            auto previous = o.*(first.member);
            SetWindowTextW(first.edit, L"0");
            SendMessageW(window, WM_TIMER, 1, 0);
            Require(o.*(first.member) == previous && !state.reset, "Invalid value changed live state");
            SetWindowTextW(first.edit, L"9999999999");
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(classroom::Apply, BN_CLICKED), 0);
            Require(o.*(first.member) == previous, "Out-of-range value accepted");
            for (auto &p : state.parameters) {
                uint32_t value = p.member == &Options::count ? 10003 :
                    p.member == &Options::draws ? 101 : p.member == &Options::workers ? 3 : 2;
                SetWindowTextW(p.edit, std::to_wstring(value).c_str());
            }
            SendMessageW(window, WM_TIMER, 1, 0);
            Require(state.reset, "Debounced apply failed");
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(classroom::Pause, BN_CLICKED), 0);
            Require(!state.paused, "Resume button failed");
        }
        if (initializations == 2) {
            const auto snapshotCount = state.previous.averages.count[RollingMetrics::Cpu];
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(classroom::Apply, BN_CLICKED), 0);
            Require(!state.reset && state.previous.averages.count[RollingMetrics::Cpu] == snapshotCount,
                    "Applying unchanged settings replaced the baseline");
        }
        SendDlgItemMessageW(window, classroom::Mode, CB_SETCURSEL, initializations == 1 ? 1 : 0, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(classroom::Mode, CBN_SELCHANGE), 0);
        Require(state.reset, "Mode change did not request reset");
    }
    void Validate(Device &gpu, const Options &o, uint64_t steps) override {
        Require(initializations == 3, "Both GUI mode transitions must recreate resources");
        Require(steps == 14, "Measurement segment not reset correctly");
        inner->Validate(gpu, o, steps);
    }
};
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::unique_ptr<Lesson> lesson;
    switch (atoi(argv[1])) {
    case 1: lesson = std::make_unique<Pipeline>(); break;
    case 2: lesson = std::make_unique<Timing>(); break;
    case 3: lesson = std::make_unique<Upload>(); break;
    case 4: lesson = std::make_unique<CpuJobs>(); break;
    case 5: lesson = std::make_unique<Compute>(); break;
    case 6: lesson = std::make_unique<Readback>(); break;
    case 7: lesson = std::make_unique<Deferred>(); break;
    case 8: lesson = std::make_unique<Integrated>(); break;
    default: return 2;
    }
    std::vector<std::string> args = {"gui_controls", "--warp", "--debug", "--validate", "--no-vsync",
        "--frames", "24", "--count", "4096", "--draws", "64"};
    std::vector<char *> pointers;
    for (auto &s : args) pointers.push_back(s.data());
    return Run(static_cast<int>(pointers.size()), pointers.data(), std::make_unique<Exercise>(std::move(lesson)));
}
