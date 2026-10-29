#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lab {
enum class Mode { Serial, Static, Dynamic };
const char* ModeName(Mode mode);
struct Config {
    int width = 640, height = 360, samples = 32, bounces = 5, tile = 32;
    Mode mode = Mode::Dynamic;
};
bool SameWork(const Config& a, const Config& b);
void Validate(const Config& c);
double NowMs();
unsigned HardwareThreads();
struct TileView {
    int x = 0, y = 0, w = 0, h = 0, owner = -1, state = 0;
    double begin = 0, end = 0;
};
struct WorkerStats { double cpuMs = 0; int tiles = 0; };
struct Snapshot {
    Config config;
    bool running = false, cancelled = false;
    int completed = 0, total = 0, workers = 0;
    double wallMs = 0, cpuMs = 0;
    std::vector<TileView> tiles;
    std::vector<WorkerStats> stats;
};
struct Result {
    Snapshot snapshot;
    std::vector<uint32_t> pixels;
    uint64_t checksum = 0;
};

// Owner-thread API: Start / Poll / Image / Wait / Cancel / Finish. A persistent
// pool processes one immutable frame at a time; workers never touch GUI objects.
class Engine {
public:
    explicit Engine(unsigned workers);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    void Start(const Config& config);
    void Cancel() noexcept;
    void Wait();
    Snapshot Poll() const;
    std::vector<uint32_t> Image() const;
    Result Finish();
    unsigned Size() const { return static_cast<unsigned>(threads_.size()); }
private:
    struct Tile {
        TileView view;
        // state is the publication boundary. Pixels/start/end are never read
        // until the corresponding release store has been acquired by the GUI.
        std::atomic<int> state{0}; // 0 queued, 1 running, 2 complete, 3 cancelled
    };
    void Worker(unsigned id);
    void Process(int index, unsigned id);
    mutable std::mutex mutex_;
    std::condition_variable work_, done_;
    std::vector<std::thread> threads_;
    std::vector<WorkerStats> stats_;
    std::unique_ptr<Tile[]> tiles_;
    std::vector<uint32_t> pixels_;
    Config config_;
    std::atomic<int> next_{0}, completed_{0};
    std::atomic<bool> cancel_{false};
    int tileCount_ = 0, remaining_ = 0;
    uint64_t generation_ = 0;
    bool stop_ = false, running_ = false;
    double start_ = 0, end_ = 0;
    std::exception_ptr error_;
};

bool RenderTile(const Config& c, const TileView& tile, std::vector<uint32_t>& pixels,
                const std::atomic<bool>& cancel);
uint64_t Checksum(const std::vector<uint32_t>& pixels);
void SaveImage(const Result& result, const std::filesystem::path& path);
void SaveTrace(const Result& result, const std::filesystem::path& path);
void SaveReport(const Result& result, const std::filesystem::path& path);
} // namespace lab
