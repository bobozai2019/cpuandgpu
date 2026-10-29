#include "classroom.h"
using namespace classroom;
class Compute final : public Lesson {
    ParticleRenderer renderer_;
    ComputeParticles particles_;
    std::vector<Particle> cpu_;

  public:
    LessonInfo Info() const override {
        return {L"05 · GPU 粒子计算",
                L"用 HLSL 批量更新粒子，结果直接用于绘制。",
                L"StructuredBuffer / SRV / UAV\nnumthreads / Dispatch / 越界保护\nGPU "
                L"数据驻留与绑定冲突",
                L"M 比较 GPU 驻留与 CPU 更新上传。\n尝试 --count 10003，验证尾组边界。\n启动 "
                L"--validate 可与 CPU 参考比对。",
                {L"Compute Shader 更新", L"CPU 更新并上传"},
                "docs/05_compute.md"};
    }
    void Init(Device &gpu, const Options &o) override {
        renderer_.Init(gpu, o.count);
        particles_.Init(gpu, o.count);
        cpu_ = InitialParticles(o.count);
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float dt) override {
        auto start = Clock::now();
        if (o.mode == 0) {
            particles_.Step(gpu, renderer_, dt);
            m.updateMs = Ms(start);
            renderer_.Gpu(gpu, particles_.srv.Get(), o.count, m);
        } else {
            UpdateParticles(cpu_, 0, cpu_.size(), dt);
            m.updateMs = Ms(start);
            renderer_.Cpu(gpu, cpu_, m);
        }
    }
    void Validate(Device &gpu, const Options &o, uint64_t frames) override {
        auto expected = InitialParticles(o.count);
        for (uint64_t i = 0; i < frames; ++i)
            UpdateParticles(expected, 0, expected.size(), 1.0f / 60.0f);
        VerifyParticles(o.mode == 0 ? particles_.ReadBlocking(gpu) : cpu_, expected);
    }
};
int main(int argc, char **argv) {
    return Run(argc, argv, std::make_unique<Compute>());
}
