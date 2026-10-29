#include "engine.h"
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace lab {
double NowMs() {
    static const double frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart); }();
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return double(t.QuadPart) * 1000.0 / frequency;
}
static uint64_t ThreadCpuTicks() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        throw std::runtime_error("GetThreadTimes failed");
    auto value = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return value(kernel) + value(user);
}
unsigned HardwareThreads() { return std::max(1u, std::thread::hardware_concurrency()); }
const char* ModeName(Mode mode) {
    switch (mode) { case Mode::Serial: return "serial"; case Mode::Static: return "static"; default: return "dynamic"; }
}
bool SameWork(const Config& a, const Config& b) {
    return a.width == b.width && a.height == b.height && a.samples == b.samples && a.bounces == b.bounces;
}
void Validate(const Config& c) {
    if (c.width < 1 || c.width > 1920 || c.height < 1 || c.height > 1080 ||
        c.samples < 1 || c.samples > 256 || c.bounces < 1 || c.bounces > 12 || c.tile < 4 || c.tile > 256 ||
        (c.mode != Mode::Serial && c.mode != Mode::Static && c.mode != Mode::Dynamic))
        throw std::invalid_argument("width 1..1920, height 1..1080, samples 1..256, bounces 1..12, tile 4..256 required");
}
Engine::Engine(unsigned workers) {
    if (workers < 1 || workers > 64) throw std::invalid_argument("workers must be 1..64");
    stats_.resize(workers);
    try {
        for (unsigned i = 0; i < workers; ++i) threads_.emplace_back([this, i] { Worker(i); });
    } catch (...) {
        { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
        work_.notify_all();
        for (auto& thread : threads_) thread.join();
        throw;
    }
}
Engine::~Engine() {
    Cancel();
    { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; }
    work_.notify_all();
    for (auto& thread : threads_) thread.join();
}
void Engine::Start(const Config& config) {
    Validate(config);
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) throw std::logic_error("wait for the current frame before starting another");
    config_ = config;
    const int nx = (config.width + config.tile - 1) / config.tile;
    const int ny = (config.height + config.tile - 1) / config.tile;
    tileCount_ = nx * ny;
    tiles_ = std::make_unique<Tile[]>(tileCount_);
    pixels_.assign(size_t(config.width) * config.height, 0x101b2c);
    for (int i = 0; i < tileCount_; ++i) {
        auto& t = tiles_[i].view;
        t.x = (i % nx) * config.tile; t.y = (i / nx) * config.tile;
        t.w = std::min(config.tile, config.width - t.x); t.h = std::min(config.tile, config.height - t.y);
    }
    stats_.assign(Size(), {});
    next_ = 0; completed_ = 0; cancel_ = false; error_ = nullptr;
    remaining_ = static_cast<int>(Size());
    running_ = true;
    // Allocation, scene setup and thread creation are outside the timed batch.
    start_ = NowMs(); end_ = start_;
    ++generation_;
    work_.notify_all();
}
void Engine::Cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }
void Engine::Wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return !running_; });
    if (error_) std::rethrow_exception(error_);
}
void Engine::Process(int index, unsigned id) {
    auto& t = tiles_[index];
    t.view.owner = static_cast<int>(id); t.view.begin = NowMs() - start_;
    t.state.store(1, std::memory_order_release);
    const bool complete = RenderTile(config_, t.view, pixels_, cancel_);
    t.view.end = NowMs() - start_;
    t.state.store(complete ? 2 : 3, std::memory_order_release);
    if (complete) completed_.fetch_add(1, std::memory_order_relaxed);
}
void Engine::Worker(unsigned id) {
    uint64_t seen = 0;
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        work_.wait(lock, [&] { return stop_ || seen != generation_; });
        if (stop_) return;
        seen = generation_;
        lock.unlock();
        WorkerStats stats;
        std::exception_ptr failure;
        try {
            const uint64_t cpuStart = ThreadCpuTicks();
            auto run = [&](int i) { Process(i, id); if (tiles_[i].state.load() == 2) ++stats.tiles; };
            if (config_.mode == Mode::Dynamic) {
                while (!cancel_.load(std::memory_order_relaxed)) {
                    const int i = next_.fetch_add(1, std::memory_order_relaxed);
                    if (i >= tileCount_) break;
                    run(i);
                }
            } else {
                // Contiguous image bands intentionally expose sky/ground cost imbalance.
                const int active = config_.mode == Mode::Serial ? 1 : static_cast<int>(Size());
                if (int(id) < active) {
                    const int first = tileCount_ * int(id) / active;
                    const int last = tileCount_ * (int(id) + 1) / active;
                    for (int i = first; i < last && !cancel_.load(std::memory_order_relaxed); ++i) run(i);
                }
            }
            stats.cpuMs = double(ThreadCpuTicks() - cpuStart) / 10000.0;
        } catch (...) { failure = std::current_exception(); cancel_ = true; }
        lock.lock();
        stats_[id] = stats;
        if (failure && !error_) error_ = failure;
        if (--remaining_ == 0) {
            end_ = NowMs(); running_ = false;
            done_.notify_all();
        }
    }
}
Snapshot Engine::Poll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot s;
    s.config = config_; s.running = running_; s.cancelled = cancel_.load();
    s.workers = config_.mode == Mode::Serial ? 1 : int(Size());
    s.total = tileCount_; s.completed = completed_.load();
    s.wallMs = start_ ? (running_ ? NowMs() : end_) - start_ : 0;
    s.stats = stats_;
    for (const auto& stat : stats_) s.cpuMs += stat.cpuMs;
    s.tiles.reserve(tileCount_);
    for (int i = 0; i < tileCount_; ++i) {
        const auto& t = tiles_[i];
        TileView v;
        v.x = t.view.x; v.y = t.view.y; v.w = t.view.w; v.h = t.view.h;
        v.state = t.state.load(std::memory_order_acquire);
        if (v.state) {
            v.owner = t.view.owner; v.begin = t.view.begin;
            // Do not copy view wholesale: the worker may still be writing end.
            v.end = v.state == 1 ? s.wallMs : t.view.end;
        }
        s.tiles.push_back(v);
    }
    return s;
}
std::vector<uint32_t> Engine::Image() const {
    std::vector<uint32_t> copy(pixels_.size(), 0x101b2c);
    for (int i = 0; i < tileCount_; ++i) {
        const auto& t = tiles_[i];
        if (t.state.load(std::memory_order_acquire) != 2) continue;
        for (int y = t.view.y; y < t.view.y + t.view.h; ++y) {
            const size_t offset = size_t(y) * config_.width + t.view.x;
            std::copy_n(pixels_.begin() + offset, t.view.w, copy.begin() + offset);
        }
    }
    return copy;
}
Result Engine::Finish() {
    Wait();
    Result r;
    r.snapshot = Poll(); r.pixels = Image();
    r.checksum = Checksum(r.pixels);
    return r;
}
uint64_t Checksum(const std::vector<uint32_t>& pixels) {
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t p : pixels) { hash ^= p; hash *= 1099511628211ull; }
    return hash;
}
} // namespace lab
