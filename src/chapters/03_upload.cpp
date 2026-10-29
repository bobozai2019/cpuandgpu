#include "classroom.h"
using namespace classroom;
class Upload final : public Lesson {
    ParticleRenderer renderer_;
    std::vector<Particle> data_;

  public:
    LessonInfo Info() const override {
        return {L"03 · 动态数据上传",
                L"在不覆盖 GPU 使用区间的前提下更新顶点。",
                L"DYNAMIC / CPU_ACCESS_WRITE\nWRITE_DISCARD / NO_OVERWRITE\n分块上传与资源重命名",
                L"两种模式都每 1024 个粒子画一次。\nM 比较每块 DISCARD 与首块 DISCARD、后续 "
                L"NO_OVERWRITE。",
                {L"每块 WRITE_DISCARD", L"DISCARD + NO_OVERWRITE"},
                "docs/03_upload.md"};
    }
    void Init(Device &gpu, const Options &o) override {
        renderer_.Init(gpu, o.count);
        data_ = InitialParticles(o.count);
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float dt) override {
        auto start = Clock::now();
        UpdateParticles(data_, 0, data_.size(), dt);
        m.updateMs = Ms(start);
        renderer_.Cpu(gpu, data_, m, o.mode == 1, true);
    }
    void Validate(Device &, const Options &o, uint64_t frames) override {
        auto expected = InitialParticles(o.count);
        for (uint64_t i = 0; i < frames; ++i)
            UpdateParticles(expected, 0, expected.size(), 1.0f / 60.0f);
        VerifyParticles(data_, expected);
    }
};
int main(int argc, char **argv) {
    return Run(argc, argv, std::make_unique<Upload>());
}
