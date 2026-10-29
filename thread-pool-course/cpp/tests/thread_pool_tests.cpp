#include "thread_pool.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <string>

using course::ThreadPool;
using namespace std::chrono_literals;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class E, class F> void throws(F&& fn) {
    bool caught = false;
    try { fn(); } catch (const E&) { caught = true; }
    check(caught, "expected exception was not raised");
}
void values_and_exceptions() {
    ThreadPool pool(1);
    check(pool.submit([] { return 42; }).get() == 42, "return value");
    auto unique = std::make_unique<int>(7);
    check(pool.submit([p = std::move(unique)] { return *p; }).get() == 7, "move-only callable");
    int written = 0;
    pool.submit([&] { written = 9; }).get();
    check(written == 9, "void result visibility");
    auto bad = pool.submit([]() -> int { throw std::range_error("expected"); });
    throws<std::range_error>([&] { (void)bad.get(); });
    check(pool.submit([] { return 5; }).get() == 5, "worker survives exception");
    pool.wait_idle();
    check(pool.stats().completed == 5, "failed task counts as completed");
}
void concurrent_producers() {
    ThreadPool pool(4);
    std::array<std::atomic<int>, 400> hits{};
    std::vector<std::future<void>> producers;
    for (int p = 0; p < 4; ++p) producers.push_back(std::async(std::launch::async, [&, p] {
        std::vector<std::future<void>> results;
        for (int i = 0; i < 100; ++i)
            results.push_back(pool.submit([&, id = p * 100 + i] { ++hits[id]; }));
        for (auto& result : results) result.get();
    }));
    for (auto& producer : producers) producer.get();
    pool.wait_idle();
    for (auto& hit : hits) check(hit == 1, "each task must run exactly once");
    auto s = pool.stats();
    check(s.submitted == 400 && s.completed == 400 && s.active == 0 && s.queued == 0, "stats invariant");
}
void bounded_parallelism_and_idle() {
    ThreadPool pool(3);
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::mutex started_mutex;
    std::condition_variable started_cv;
    int started = 0;
    std::vector<std::future<void>> jobs;
    for (int i = 0; i < 12; ++i) jobs.push_back(pool.submit([&] {
        { std::lock_guard<std::mutex> lock(started_mutex); ++started; started_cv.notify_all(); }
        gate.wait();
    }));
    bool all_started;
    { std::unique_lock<std::mutex> lock(started_mutex);
      all_started = started_cv.wait_for(lock, 5s, [&] { return started == 3; }); }
    const auto s = pool.stats();
    std::promise<void> waiting;
    auto waiting_signal = waiting.get_future();
    auto idle = std::async(std::launch::async, [&] { waiting.set_value(); pool.wait_idle(); });
    waiting_signal.wait();
    bool blocked = idle.wait_for(30ms) == std::future_status::timeout;
    release.set_value(); // 在断言前释放 gate，失败路径也能清理线程。
    for (auto& job : jobs) job.get();
    idle.get();
    check(all_started && s.active == 3 && s.queued == 9, "fixed worker concurrency");
    check(blocked, "idle must wait for active jobs");
    check(pool.stats().completed == 12, "all gated jobs complete");
}
void empty_queue_is_not_idle() {
    ThreadPool pool(1);
    std::promise<void> release, started;
    auto gate = release.get_future().share();
    auto begun = started.get_future();
    auto job = pool.submit([&] { started.set_value(); gate.wait(); });
    begun.wait();
    auto s = pool.stats();
    auto idle = std::async(std::launch::async, [&] { pool.wait_idle(); });
    bool blocked = idle.wait_for(30ms) == std::future_status::timeout;
    release.set_value();
    job.get(); idle.get();
    check(s.queued == 0 && s.active == 1 && blocked, "empty queue is not idle");
}
void shutdown_races_and_drains() {
    ThreadPool pool(4);
    std::atomic<int> accepted{0}, executed{0};
    // 先保证至少有一些已接收的任务，再让生产者与关闭并发。
    for (int i = 0; i < 20; ++i) { (void)pool.submit([&] { ++executed; }); ++accepted; }
    std::promise<void> release;
    auto gate = release.get_future().share();
    auto producer = std::async(std::launch::async, [&] {
        gate.wait();
        for (int i = 0; i < 300; ++i) {
            try { (void)pool.submit([&] { ++executed; }); ++accepted; }
            catch (const std::runtime_error&) { break; }
        }
    });
    auto closer1 = std::async(std::launch::async, [&] { gate.wait(); pool.shutdown(); });
    auto closer2 = std::async(std::launch::async, [&] { gate.wait(); pool.shutdown(); });
    release.set_value();
    producer.get(); closer1.get(); closer2.get();
    pool.shutdown();
    throws<std::runtime_error>([&] { (void)pool.submit([] {}); });
    check(accepted == executed && pool.completed() == static_cast<std::uint64_t>(accepted.load()), "drain accepted work");
    check(!pool.stats().accepting, "closed state");
}
void own_worker_guards() {
    ThreadPool pool(1);
    auto idle = pool.submit([&] { pool.wait_idle(); });
    throws<std::logic_error>([&] { idle.get(); });
    auto close = pool.submit([&] { pool.shutdown(); });
    throws<std::logic_error>([&] { close.get(); });
    check(pool.submit([] { return 1; }).get() == 1, "guard must not kill worker");
}
void lifecycle() {
    throws<std::invalid_argument>([] { ThreadPool invalid(0); });
    throws<std::invalid_argument>([] { ThreadPool invalid(65); });
    { ThreadPool empty(2); empty.wait_idle(); empty.shutdown(); empty.shutdown(); }
    std::atomic<int> count{0};
    { ThreadPool pool(2); for (int i = 0; i < 80; ++i) (void)pool.submit([&] { ++count; }); }
    check(count == 80, "destructor drains work");
}
int main() {
    const std::pair<const char*, void(*)()> tests[] = {
        {"values/exceptions/move-only", values_and_exceptions}, {"concurrent producers/exactly once", concurrent_producers},
        {"bounded parallelism/wait idle", bounded_parallelism_and_idle}, {"empty queue vs active task", empty_queue_is_not_idle},
        {"concurrent shutdown/drain/reject", shutdown_races_and_drains}, {"own-worker guards", own_worker_guards},
        {"validation/destructor/empty pool", lifecycle}
    };
    try {
        for (auto [name, test] : tests) { test(); std::cout << "PASS " << name << '\n'; }
        std::cout << "7/7 test groups passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
