#include "classroom.h"
using namespace classroom;
class Deferred final : public Lesson {
    ParticleRenderer renderer_;
    std::unique_ptr<Jobs> jobs_;
    std::vector<ComPtr<ID3D11DeviceContext>> contexts_;
    std::vector<ComPtr<ID3D11CommandList>> lists_;

  public:
    LessonInfo Info() const override {
        return {L"07 · 多线程命令录制",
                L"区分 CPU 并行录制与最终串行回放。",
                L"Deferred Context / Command List\nFinishCommandList / "
                L"ExecuteCommandList\n驱动能力 / 完整状态绑定",
                L"两种模式绘制相同数量三角形。\n改变 --draws 与 "
                L"--workers。\n观察低负载时并行调度开销。",
                {L"Immediate 单线程录制", L"Deferred 多线程录制"},
                "docs/07_deferred.md"};
    }
    void Init(Device &gpu, const Options &o) override {
        renderer_.Init(gpu, o.count);
        renderer_.InitGrid(gpu, o.draws);
        jobs_ = std::make_unique<Jobs>(o.workers);
        contexts_.clear();
        contexts_.resize(o.workers);
        lists_.clear();
        lists_.resize(o.workers);
        for (auto &ctx : contexts_)
            Check(gpu.device->CreateDeferredContext(0, &ctx), "CreateDeferredContext");
        D3D11_FEATURE_DATA_THREADING support{};
        Check(gpu.device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &support, sizeof(support)),
              "Threading support");
        std::cout << "DriverCommandLists=" << support.DriverCommandLists
                  << " DriverConcurrentCreates=" << support.DriverConcurrentCreates << '\n';
    }
    void Frame(Device &gpu, const Options &o, Metrics &m, float) override {
        if (o.mode == 0) {
            renderer_.Grid(gpu, o.draws, m);
            return;
        }
        auto start = Clock::now();
        jobs_->Run([&](uint32_t worker) {
            auto *ctx = contexts_[worker].Get();
            ctx->ClearState();
            gpu.BindTarget(ctx);
            renderer_.GridState(ctx);
            UINT first = o.draws * worker / jobs_->Size(),
                 last = o.draws * (worker + 1) / jobs_->Size();
            for (UINT i = first; i < last; ++i)
                ctx->Draw(3, i * 3);
            Check(ctx->FinishCommandList(FALSE, lists_[worker].ReleaseAndGetAddressOf()),
                  "FinishCommandList");
        });
        m.updateMs = Ms(start);
        // 只有渲染线程访问 immediate context；并行的是上面的录制过程。
        for (auto &list : lists_)
            gpu.context->ExecuteCommandList(list.Get(), FALSE);
        m.drawCalls = o.draws;
    }
};
int main(int argc, char **argv) {
    return Run(argc, argv, std::make_unique<Deferred>());
}
