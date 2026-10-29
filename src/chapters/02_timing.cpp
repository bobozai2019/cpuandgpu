#include "classroom.h"
using namespace classroom;
class Timing final : public Lesson {
    ParticleRenderer renderer_;
    ComputeParticles particles_;

  public:
    LessonInfo Info() const override {
        return {L"02 · 正确测量 GPU",
                L"把 CPU 调用耗时与 GPU 时间戳区间分开。",
                L"TIMESTAMP / TIMESTAMP_DISJOINT\n16 槽查询环 / GetData\nS_FALSE 表示尚未完成",
                L"观察 GPU 样本所属的旧帧编号。\nupdate_ms 是 Draw 调用耗时，gpu_interval_ms 是 "
                L"GPU 时间戳区间。",
                {L"延后获取时间戳", L"每帧强制等待（反例）"},
                "docs/02_timing.md"};
    }
    void Init(Device &gpu, const Options &o) override {
        renderer_.Init(gpu, o.count);
        particles_.Init(gpu, o.count);
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float) override {
        const auto start = Clock::now();
        for (UINT i = 0; i < o.gpuRepeats; ++i)
            renderer_.Gpu(gpu, particles_.srv.Get(), o.count, m);
        m.updateMs = Ms(start); // 只能说明 CPU 调用成本，不能据此声称 GPU 已经计算完。
        if (o.mode == 1)
            gpu.WaitIdle();
        // 时间戳录制/轮询在 common/gpu.cpp::GpuTimer，主循环负责正确归属源帧。
    }
};
int main(int argc, char **argv) {
    return Run(argc, argv, std::make_unique<Timing>());
}
