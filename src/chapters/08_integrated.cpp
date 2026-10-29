#include "classroom.h"
using namespace classroom;
class Integrated final : public Lesson {
    ParticleRenderer renderer_;
    ComputeParticles particles_;
    ReadbackRing ring_;
    std::vector<Particle> actors_;
    std::unique_ptr<Jobs> jobs_;
    ComPtr<ID3D11Buffer> staging_;
    std::vector<Particle> readback_;
    uint64_t baseFrame_ = 0, steps_ = 0;

  public:
    LessonInfo Info() const override {
        return {L"08 · 综合并行实验",
                L"把 CPU 游戏对象与 GPU 视觉粒子组成一帧。",
                L"CPU 独立任务 / GPU 驻留\n先提交 GPU，再处理 CPU 工作\n延迟统计数据与帧时间",
                L"两种模式每帧做同样的更新与绘制。\nM 比较串行 + 立即回读，与任务并行 + 延后回读。",
                {L"并行组织 + 延后回读", L"串行组织 + 立即回读"},
                "docs/08_integrated.md"};
    }
    void Init(Device &gpu, const Options &o) override {
        renderer_.Init(gpu, o.count);
        particles_.Init(gpu, o.count);
        ring_.Init(gpu, particles_.buffer.Get(), o.count);
        actors_ = InitialParticles(o.count);
        jobs_ = std::make_unique<Jobs>(o.workers);
        readback_.resize(o.count);
        steps_ = 0;
        D3D11_BUFFER_DESC bd{};
        particles_.buffer->GetDesc(&bd);
        bd.Usage = D3D11_USAGE_STAGING;
        bd.BindFlags = 0;
        bd.MiscFlags = 0;
        bd.StructureByteStride = 0;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(gpu.device->CreateBuffer(&bd, nullptr, staging_.ReleaseAndGetAddressOf()),
              "Integrated staging");
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float dt) override {
        if (steps_++ == 0)
            baseFrame_ = m.frame;
        if (o.mode == 0) {
            // 先录入独立的 GPU 工作。是否立即送达硬件仍由运行时/驱动决定。
            particles_.Step(gpu, renderer_, dt);
            renderer_.Gpu(gpu, particles_.srv.Get(), o.count, m);
            auto start = Clock::now();
            jobs_->Run([&](uint32_t id) {
                UpdateParticles(actors_, actors_.size() * id / jobs_->Size(),
                                actors_.size() * (id + 1) / jobs_->Size(), dt);
            });
            m.updateMs = Ms(start);
            ring_.Poll(gpu, m.frame, m);
            ring_.Enqueue(gpu, particles_.buffer.Get(), m.frame, m);
        } else {
            auto start = Clock::now();
            UpdateParticles(actors_, 0, actors_.size(), dt);
            m.updateMs = Ms(start);
            particles_.Step(gpu, renderer_, dt);
            renderer_.Gpu(gpu, particles_.srv.Get(), o.count, m);
            start = Clock::now();
            gpu.context->CopyResource(staging_.Get(), particles_.buffer.Get());
            D3D11_MAPPED_SUBRESOURCE map{};
            Check(gpu.context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &map),
                  "Integrated blocking readback");
            memcpy(readback_.data(), map.pData, static_cast<size_t>(o.count) * sizeof(Particle));
            gpu.context->Unmap(staging_.Get(), 0);
            m.readbackMs = Ms(start);
            m.readbackAge = 0;
        }
        // 两条路径都有一份 CPU 数据上传和一份 GPU 数据绘制，输出工作量一致。
        renderer_.Cpu(gpu, actors_, m);
    }
    void Validate(Device &gpu, const Options &o, uint64_t frames) override {
        auto expected = InitialParticles(o.count);
        for (uint64_t i = 0; i < frames; ++i)
            UpdateParticles(expected, 0, expected.size(), 1.0f / 60.0f);
        VerifyParticles(actors_, expected);
        VerifyParticles(particles_.ReadBlocking(gpu), expected);
        if (o.mode == 1)
            VerifyParticles(readback_, expected);
        else {
            Metrics drain;
            ring_.Poll(gpu, baseFrame_ + frames, drain);
            if (!ring_.consumed)
                throw std::runtime_error("Integrated readback ring never completed");
            auto historical = InitialParticles(o.count);
            for (uint64_t i = 0; i <= ring_.latestFrame - baseFrame_; ++i)
                UpdateParticles(historical, 0, historical.size(), 1.0f / 60.0f);
            VerifyParticles(ring_.latest, historical);
        }
    }
};
int main(int argc, char **argv) {
    return Run(argc, argv, std::make_unique<Integrated>());
}
