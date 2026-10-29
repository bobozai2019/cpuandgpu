#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace course {
// 固定工作线程 + FIFO 队列。只接收无参数的同步 callable，参数用 lambda 捕获。
// 所有者必须在池外销毁对象，并保证其他调用者已经停止访问该对象。
class ThreadPool final {
public:
    struct Stats {
        std::size_t queued, active;
        std::uint64_t submitted, completed;
        bool accepting;
    };

    explicit ThreadPool(std::size_t count) {
        if (count == 0 || count > 64) throw std::invalid_argument("workers must be 1..64");
        workers_.reserve(count);
        try {
            for (std::size_t i = 0; i < count; ++i)
                workers_.emplace_back([this] { worker_loop(); });
        } catch (...) {
            // 构造中途创建线程失败，也必须唤醒并 join 已经创建的线程。
            { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
            ready_.notify_all();
            for (auto& worker : workers_) worker.join();
            throw;
        }
    }
    ~ThreadPool() { shutdown(); }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<class F>
    auto submit(F&& fn) -> std::future<std::invoke_result_t<std::decay_t<F>&>> {
        using R = std::invoke_result_t<std::decay_t<F>&>;
        // packaged_task 保存返回值或异常；shared_ptr 使队列中的包装 lambda 可复制。
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        auto result = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) throw std::runtime_error("pool is closed");
            tasks_.emplace([task] { (*task)(); });
            ++submitted_; // 入队成功才算接收，和关闭检查处于同一临界区。
        }
        ready_.notify_one();
        return result;
    }

    void wait_idle() {
        require_external_caller();
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this] { return tasks_.empty() && active_ == 0; });
        // 这里只保证观察到了空闲；其他生产者随后仍可提交任务。
    }

    void shutdown() {
        require_external_caller();
        // 多个外部调用者可同时关闭，但 join 只能由一个调用者依次执行。
        std::lock_guard<std::mutex> close_lock(shutdown_mutex_);
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
        ready_.notify_all();
        // 不持有队列锁 join，否则 worker 无法取得队列锁并退出。
        for (auto& worker : workers_)
            if (worker.joinable()) worker.join();
    }

    std::uint64_t completed() const noexcept {
        // 仅供进度显示，不发布任务结果；结果应通过 future.get() 读取。
        return completed_.load(std::memory_order_relaxed);
    }
    Stats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {tasks_.size(), active_, submitted_, completed(), !stopping_};
    }

private:
    void require_external_caller() const {
        if (current_pool_ == this)
            throw std::logic_error("a pool worker must not wait_idle/shutdown its own pool");
    }
    void worker_loop() {
        current_pool_ = this;
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) break; // 已接收的任务必须排空。
                job = std::move(tasks_.front());
                tasks_.pop();
                ++active_;
            }
            job(); // 锁外执行！用户异常由 packaged_task 收集，工作线程继续服务。
            {
                std::lock_guard<std::mutex> lock(mutex_);
                completed_.fetch_add(1, std::memory_order_relaxed);
                --active_;
                if (tasks_.empty() && active_ == 0) idle_.notify_all();
            }
        }
        current_pool_ = nullptr;
    }

    mutable std::mutex mutex_;                  // 保护队列、active、submitted、stopping
    std::mutex shutdown_mutex_;                 // 串行化外部关闭，不用于执行任务
    std::condition_variable ready_, idle_;      // 分开等待“可取任务”和“全部空闲”
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    std::size_t active_ = 0;
    std::uint64_t submitted_ = 0;
    std::atomic<std::uint64_t> completed_{0};    // 可在不取队列锁时读取进度
    bool stopping_ = false;
    inline static thread_local ThreadPool* current_pool_ = nullptr;
};
} // namespace course
