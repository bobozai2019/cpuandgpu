#pragma once
#include "classroom.h"
#include <deque>

namespace classroom {
// Frame-weighted means over (now - 10, now], on a clock excluding pauses.
class RollingMetrics {
  public:
    enum Field {
        Cpu,
        Gpu,
        Update,
        Present,
        Upload,
        Readback,
        Draws,
        Age,
        Drops,
        GpuDraw,
        FieldCount
    };
    struct Summary {
        std::array<double, FieldCount> mean{};
        std::array<size_t, FieldCount> count{};
        double seconds = 0;
    };

  private:
    struct Sample {
        double time;
        uint64_t frame;
        std::array<double, FieldCount> values;
    };
    std::deque<Sample> samples_;
    std::array<double, FieldCount> sums_{};
    std::array<size_t, FieldCount> counts_{};
    double now_ = 0;
    void Accumulate(const Sample &s, int sign) {
        for (size_t i = 0; i < FieldCount; ++i)
            if (s.values[i] >= 0 && std::isfinite(s.values[i])) {
                sums_[i] += sign * s.values[i];
                if (sign > 0)
                    ++counts_[i];
                else
                    --counts_[i];
            }
    }

  public:
    void Clear() {
        samples_.clear();
        sums_.fill(0);
        counts_.fill(0);
        now_ = 0;
    }
    void Add(double seconds, const Metrics &m) {
        now_ = seconds;
        while (!samples_.empty() && samples_.front().time <= seconds - 10.0) {
            Accumulate(samples_.front(), -1);
            samples_.pop_front();
        }
        Sample s{seconds,
                 m.frame,
                 {m.cpuMs, m.gpuMs, m.updateMs, m.presentMs, m.uploadMs, m.readbackMs,
                  static_cast<double>(m.drawCalls), static_cast<double>(m.readbackAge),
                  static_cast<double>(m.droppedReadbacks), m.gpuDrawMs}};
        Accumulate(s, 1);
        samples_.push_back(s);
    }
    void GpuResult(uint64_t frame, double ms, Field field = Gpu) {
        if (field != Gpu && field != GpuDraw)
            return;
        if (samples_.empty() || frame < samples_.front().frame || frame > samples_.back().frame ||
            ms < 0 || !std::isfinite(ms))
            return;
        auto &s = samples_[static_cast<size_t>(frame - samples_.front().frame)];
        if (s.values[field] >= 0)
            sums_[field] -= s.values[field];
        else
            ++counts_[field];
        s.values[field] = ms;
        sums_[field] += ms;
    }
    Summary Get() const {
        Summary result;
        result.count = counts_;
        result.seconds = std::min(10.0, now_);
        for (size_t i = 0; i < FieldCount; ++i)
            result.mean[i] = counts_[i] ? sums_[i] / counts_[i] : -1;
        return result;
    }
};
} // namespace classroom
