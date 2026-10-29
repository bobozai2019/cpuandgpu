#include "thread_pool.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>

using course::ThreadPool;
using Clock = std::chrono::steady_clock;
struct Options { int workers = 4, tasks = 16, iterations = 20000; std::string demo = "all"; };

int number(const std::string& value, int limit) {
    std::size_t used = 0;
    int n = std::stoi(value, &used);
    if (used != value.size() || n < 1 || n > limit) throw std::invalid_argument("number out of range");
    return n;
}
Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (i + 1 >= argc) throw std::invalid_argument("missing option value");
        std::string value = argv[++i];
        if (key == "--workers") o.workers = number(value, 64);
        else if (key == "--tasks") o.tasks = number(value, 10000);
        else if (key == "--iterations") o.iterations = number(value, 1000000);
        else if (key == "--demo") o.demo = value;
        else throw std::invalid_argument("unknown option: " + key);
    }
    if (o.demo != "all" && o.demo != "pool" && o.demo != "counter" && o.demo != "benchmark")
        throw std::invalid_argument("demo must be all|pool|counter|benchmark");
    return o;
}
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::uint64_t work(int seed, int iterations) {
    std::uint64_t x = static_cast<std::uint64_t>(seed) + 1;
    for (int i = 0; i < iterations; ++i) x = x * 1664525ULL + 1013904223ULL;
    return x; // 无符号溢出按模 2^64 定义，结果参与校验，避免无用循环被消除。
}
void pool_demo(const Options& o) {
    ThreadPool pool(o.workers);
    std::vector<std::future<int>> results;
    for (int i = 0; i < o.tasks; ++i) results.push_back(pool.submit([i] { return i * i; }));
    std::int64_t sum = 0;
    for (auto& result : results) sum += result.get();
    const std::int64_t n = o.tasks;
    if (sum != n * (n - 1) * (2 * n - 1) / 6) throw std::runtime_error("incorrect square sum");
    auto failure = pool.submit([]() -> int { throw std::runtime_error("teaching exception"); });
    try { (void)failure.get(); } catch (const std::runtime_error& e) {
        std::cout << "[pool] future received: " << e.what() << '\n';
    }
    auto still_alive = pool.submit([] { return 42; });
    if (still_alive.get() != 42) throw std::runtime_error("worker did not survive");
    pool.wait_idle();
    auto s = pool.stats();
    std::cout << "[pool] square sum=" << sum << " completed=" << s.completed
              << " queued=" << s.queued << " active=" << s.active << '\n';
    pool.shutdown(); // 也可由析构自动调用。
    try { (void)pool.submit([] {}); throw std::logic_error("accepted after close"); }
    catch (const std::runtime_error&) { std::cout << "[pool] closed pool rejects new work: PASS\n"; }
}
void counter_demo(const Options& o) {
    ThreadPool pool(o.workers);
    std::mutex counter_mutex;
    std::uint64_t locked = 0;
    std::atomic<std::uint64_t> atomic{0};
    const auto expected = static_cast<std::uint64_t>(o.tasks) * o.iterations;
    for (bool use_atomic : {false, true}) {
        std::vector<std::future<void>> results;
        auto start = Clock::now();
        for (int t = 0; t < o.tasks; ++t) results.push_back(pool.submit([&] {
            for (int i = 0; i < o.iterations; ++i) {
                if (use_atomic) atomic.fetch_add(1, std::memory_order_relaxed);
                else { std::lock_guard<std::mutex> lock(counter_mutex); ++locked; }
            }
        }));
        for (auto& result : results) result.get();
        auto count = use_atomic ? atomic.load(std::memory_order_relaxed) : locked;
        if (count != expected) throw std::runtime_error("counter mismatch");
        std::cout << "[counter] " << (use_atomic ? "atomic" : "mutex") << " count=" << count
                  << " expected=" << expected << " ms=" << elapsed(start) << '\n';
    }
}
void benchmark(const Options& o) {
    ThreadPool pool(o.workers);
    pool.submit([] { return work(0, 1000); }).get(); // 小规模预热，不计入计时。
    auto start = Clock::now();
    std::uint64_t sequential = 0;
    for (int i = 0; i < o.tasks; ++i) sequential ^= work(i, o.iterations);
    auto serial_ms = elapsed(start);
    start = Clock::now();
    std::vector<std::future<std::uint64_t>> results;
    for (int i = 0; i < o.tasks; ++i)
        results.push_back(pool.submit([i, &o] { return work(i, o.iterations); }));
    std::uint64_t parallel = 0;
    for (auto& result : results) parallel ^= result.get();
    auto parallel_ms = elapsed(start);
    if (sequential != parallel) throw std::runtime_error("checksum mismatch");
    std::cout << "[benchmark] serial_ms=" << serial_ms << " pool_ms=" << parallel_ms
              << " checksum=" << parallel << " match=PASS\n";
    std::cout << "Timing excludes pool construction, includes submit/get. Repeat Release runs; speedup is not guaranteed.\n";
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "--demo all|pool|counter|benchmark --workers 1..64 --tasks 1..10000 --iterations 1..1000000\n";
            return 0;
        }
        auto o = parse(argc, argv);
        std::cout << std::fixed << std::setprecision(3) << "C++17 thread pool | workers=" << o.workers
                  << " tasks=" << o.tasks << " iterations=" << o.iterations << '\n';
        if (o.demo == "all" || o.demo == "pool") pool_demo(o);
        if (o.demo == "all" || o.demo == "counter") counter_demo(o);
        if (o.demo == "all" || o.demo == "benchmark") benchmark(o);
        return 0;
    } catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << '\n'; return 1; }
}
