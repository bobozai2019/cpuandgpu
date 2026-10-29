#include "classroom.h"
using namespace classroom;
class CpuJobs final : public Lesson {
    ParticleRenderer renderer_;
    std::vector<Particle> data_;
    std::unique_ptr<Jobs> jobs_;

  public:
    LessonInfo Info() const override {
        return {
            L"04 · CPU 任务并行",
            L"将独立元素分给固定工作线程，并正确汇合。",
            L"持久线程池 / 连续数组\n互不重叠的写入区间\n任务粒度 / 汇合开销",
            L"M 比较串行与并行。\n改变 --count 和 --workers。\n工作线程不调用 Immediate Context。",
            {L"串行更新", L"线程池并行更新"},
            "docs/04_cpu_jobs.md"};
    }
    void Init(Device &gpu, const Options &o) override {
        renderer_.Init(gpu, o.count);
        data_ = InitialParticles(o.count);
        jobs_ = std::make_unique<Jobs>(o.workers);
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float dt) override {
        auto start = Clock::now();
        if (o.mode == 0)
            UpdateParticles(data_, 0, data_.size(), dt);
        else
            jobs_->Run([&](uint32_t worker) {
                size_t first = data_.size() * worker / jobs_->Size(),
                       last = data_.size() * (worker + 1) / jobs_->Size();
                UpdateParticles(data_, first, last, dt);
            }); // Run 返回意味着所有写入已完成，主线程此时才上传。
        m.updateMs = Ms(start);
        renderer_.Cpu(gpu, data_, m);
    }
    void Validate(Device &, const Options &o, uint64_t frames) override {
        auto expected = InitialParticles(o.count);
        for (uint64_t i = 0; i < frames; ++i)
            UpdateParticles(expected, 0, expected.size(), 1.0f / 60.0f);
        VerifyParticles(data_, expected);
    }
};
int main(int argc, char **argv) {
    return Run(argc, argv, std::make_unique<CpuJobs>());
}
