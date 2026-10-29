#include "rolling_metrics.h"
using namespace classroom;
void Require(bool ok) {
    if (!ok)
        throw std::runtime_error("Rolling mean regression");
}
bool Near(double a, double b) {
    return std::abs(a - b) < 0.000001;
}
int main() {
    RollingMetrics window;
    Require(window.Get().count[RollingMetrics::Gpu] == 0);
    Metrics a;
    a.frame = 0;
    a.cpuMs = 2;
    a.drawCalls = 1;
    window.Add(1, a);
    a.frame = 1;
    a.cpuMs = 4;
    a.drawCalls = 2;
    a.readbackAge = 3;
    a.droppedReadbacks = 1;
    window.Add(2, a);
    window.GpuResult(0, 6);
    window.GpuResult(0, 1.25, RollingMetrics::GpuDraw);
    Require(Near(window.Get().mean[RollingMetrics::GpuDraw], 1.25));
    Require(window.Get().count[RollingMetrics::GpuDraw] == 1);
    auto s = window.Get();
    Require(Near(s.mean[RollingMetrics::Cpu], 3));
    Require(Near(s.mean[RollingMetrics::Draws], 1.5));
    Require(Near(s.mean[RollingMetrics::Gpu], 6) && s.count[RollingMetrics::Gpu] == 1);
    Require(Near(s.mean[RollingMetrics::Age], 3) && s.count[RollingMetrics::Age] == 1);
    Require(Near(s.mean[RollingMetrics::Drops], 0.5));
    window.GpuResult(0, 8);
    Require(Near(window.Get().mean[RollingMetrics::Gpu], 8));
    a.frame = 2;
    a.cpuMs = 10;
    a.readbackAge = -1;
    window.Add(11, a);
    window.GpuResult(0, 100);
    window.GpuResult(0, 100, RollingMetrics::GpuDraw);
    Require(window.Get().count[RollingMetrics::GpuDraw] == 0);
    Require(window.Get().count[RollingMetrics::Gpu] == 0);
    window.GpuResult(1, 4);
    s = window.Get();
    Require(Near(s.mean[RollingMetrics::Cpu], 7) && Near(s.mean[RollingMetrics::Gpu], 4));
    Require(Near(s.seconds, 10));
    window.Clear();
    Require(window.Get().count[RollingMetrics::Cpu] == 0 && window.Get().seconds == 0);
    a.frame = 0;
    window.Add(0, a);
    window.GpuResult(0, 2);
    Require(Near(window.Get().mean[RollingMetrics::Gpu], 2));
    std::cout << "ROLLING METRICS PASS\n";
}
