#include "classroom.h"
using namespace classroom;
class Readback final : public Lesson {
    ParticleRenderer renderer_;
    ComputeParticles particles_;
    ReadbackRing ring_;
    ComPtr<ID3D11Buffer> staging_;
    std::vector<Particle> result_;
    uint64_t baseFrame_ = 0, steps_ = 0;

  public:
    LessonInfo Info() const override {
        return {
            L"06 · GPU 结果回读",
            L"比较立即读取和延后消费 GPU 数据的代价。",
            L"STAGING / CopyResource / Map\nEVENT 查询 / 完成状态\n4 槽回读环 / 背压策略",
            L"观察回读耗时和回读帧龄。\n满环时跳过采样，不覆盖在途槽位。\n延迟几帧不等于保证完成。",
            {L"查询完成后延后读取", L"每帧立即 Map 读取"},
            "docs/06_readback.md"};
    }
    void Init(Device &gpu, const Options &o) override {
        renderer_.Init(gpu, o.count);
        particles_.Init(gpu, o.count);
        ring_.Init(gpu, particles_.buffer.Get(), o.count);
        result_.resize(o.count);
        steps_ = 0;
        D3D11_BUFFER_DESC bd{};
        particles_.buffer->GetDesc(&bd);
        bd.Usage = D3D11_USAGE_STAGING;
        bd.BindFlags = 0;
        bd.MiscFlags = 0;
        bd.StructureByteStride = 0;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Check(gpu.device->CreateBuffer(&bd, nullptr, staging_.ReleaseAndGetAddressOf()),
              "Readback staging");
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float dt) override {
        if (steps_++ == 0)
            baseFrame_ = m.frame;
        particles_.Step(gpu, renderer_, dt);
        if (o.mode == 0) {
            ring_.Poll(gpu, m.frame, m);
            ring_.Enqueue(gpu, particles_.buffer.Get(), m.frame, m);
        } else {
            auto start = Clock::now();
            gpu.context->CopyResource(staging_.Get(), particles_.buffer.Get());
            D3D11_MAPPED_SUBRESOURCE map{};
            Check(gpu.context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &map),
                  "Immediate readback");
            memcpy(result_.data(), map.pData, static_cast<size_t>(o.count) * sizeof(Particle));
            gpu.context->Unmap(staging_.Get(), 0);
            m.readbackMs = Ms(start);
            m.readbackAge = 0;
        }
        renderer_.Gpu(gpu, particles_.srv.Get(), o.count, m);
    }
    void Validate(Device &gpu, const Options &o, uint64_t frames) override {
        auto expected = InitialParticles(o.count);
        for (uint64_t i = 0; i < frames; ++i)
            UpdateParticles(expected, 0, expected.size(), 1.0f / 60.0f);
        VerifyParticles(particles_.ReadBlocking(gpu), expected);
        if (o.mode == 1)
            VerifyParticles(result_, expected);
        else {
            Metrics drain;
            ring_.Poll(gpu, baseFrame_ + frames, drain);
            if (!ring_.consumed)
                throw std::runtime_error("Readback ring never consumed a sample");
            auto historical = InitialParticles(o.count);
            for (uint64_t i = 0; i <= ring_.latestFrame - baseFrame_; ++i)
                UpdateParticles(historical, 0, historical.size(), 1.0f / 60.0f);
            VerifyParticles(ring_.latest, historical);
            std::cout << "Ring consumed=" << ring_.consumed
                      << " latest source frame=" << ring_.latestFrame << '\n';
        }
    }
};
int main(int argc, char **argv) {
    return Run(argc, argv, std::make_unique<Readback>());
}
