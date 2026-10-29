#include "classroom.h"
using namespace classroom;
class Pipeline final : public Lesson {
    ParticleRenderer renderer_;
    ComputeParticles particles_;

  public:
    LessonInfo Info() const override {
        return {
            L"01 · CPU / GPU 流水线",
            L"区分 API 返回、命令提交和 GPU 完成。",
            L"Immediate Context\n驱动命令缓冲 / Present\n跨帧重叠与显式等待",
            L"M 切换是否每帧等待 GPU。\n用 --cpu-ms 4 和 --gpu-repeat 32 增加两端负载，比较 CSV。",
            {L"正常异步执行", L"每帧等待 GPU"},
            "docs/01_pipeline.md"};
    }
    void Init(Device &gpu, const Options &o) override {
        renderer_.Init(gpu, o.count);
        particles_.Init(gpu, o.count);
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float) override {
        auto start = Clock::now();
        // 模拟游戏逻辑：GPU 可以同时处理前面已提交的帧。
        volatile uint32_t work = 1;
        while (Ms(start) < o.cpuMs) {
            work = work * 1664525u + 1013904223u;
        }
        m.updateMs = Ms(start);
        for (UINT i = 0; i < o.gpuRepeats; ++i)
            renderer_.Gpu(gpu, particles_.srv.Get(), o.count, m);
        if (o.mode == 1)
            gpu.WaitIdle(); // 教学用反例：强制在每帧排空流水线。
    }
};
int main(int argc, char **argv) {
    return Run(argc, argv, std::make_unique<Pipeline>());
}
