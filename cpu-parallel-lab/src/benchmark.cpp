#include "engine.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>

int main(int argc, char** argv) {
    using namespace lab;
    try {
        Config c; c.width = 480; c.height = 270; c.samples = 32;
        int repeats = 3, maxWorkers = int(std::min(8u, HardwareThreads()));
        std::filesystem::path out = "out/benchmark";
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                std::cout << "cpu_benchmark [--width 480] [--height 270] [--samples 32] [--bounces 5]\n"
                    "  [--tile 32] [--workers 8] [--repeats 3] [--output out/benchmark]\n"
                    "Runs serial + static/dynamic 1,2,4,...,workers with alternating order.\n";
                return 0;
            }
            if (i+1 >= argc) throw std::invalid_argument("missing value for " + key);
            const std::string value = argv[++i];
            if (key == "--output") { out = value; continue; }
            size_t consumed = 0; int n = std::stoi(value, &consumed);
            if (consumed != value.size()) throw std::invalid_argument("invalid number");
            if (key == "--width") c.width = n;
            else if (key == "--height") c.height = n;
            else if (key == "--samples") c.samples = n;
            else if (key == "--bounces") c.bounces = n;
            else if (key == "--tile") c.tile = n;
            else if (key == "--workers") maxWorkers = n;
            else if (key == "--repeats") repeats = n;
            else throw std::invalid_argument("unknown option: " + key);
        }
        Validate(c);
        if (maxWorkers < 1 || maxWorkers > 64 || repeats < 1 || repeats > 20) throw std::invalid_argument("workers 1..64, repeats 1..20");
        std::filesystem::create_directories(out);
        struct Case { Mode mode; int workers; };
        std::vector<Case> cases{{Mode::Serial,1}};
        std::vector<int> counts;
        for (int w = 1; w < maxWorkers; w *= 2) counts.push_back(w);
        counts.push_back(maxWorkers);
        std::map<int,std::unique_ptr<Engine>> pools;
        for (int w : counts) {
            pools[w] = std::make_unique<Engine>(unsigned(w));
            cases.push_back({Mode::Static,w}); cases.push_back({Mode::Dynamic,w});
        }
        // Warm all persistent pools with the full workload; no warmup enters CSV.
        for (auto& entry : pools) { c.mode = Mode::Dynamic; entry.second->Start(c); entry.second->Wait(); }
        std::ofstream raw(out/"samples.csv"); raw.exceptions(std::ios::failbit | std::ios::badbit);
        raw << "repeat,mode,workers,width,height,samples,bounces,tile,wall_ms,worker_cpu_ms,average_parallelism,checksum\n";
        std::vector<std::vector<double>> times(cases.size()), cpus(cases.size());
        std::vector<uint32_t> reference;
        bool haveReference = false;
        std::cout << "Logical processors: " << HardwareThreads() << "; deterministic CPU path tracing\n";
        for (int repeat = 0; repeat < repeats; ++repeat) {
            for (size_t step = 0; step < cases.size(); ++step) {
                const size_t index = repeat%2 ? cases.size()-1-step : step;
                const auto item = cases[index]; c.mode = item.mode;
                auto& pool = *pools.at(item.workers);
                pool.Start(c); auto r = pool.Finish();
                if (!haveReference) { reference = r.pixels; haveReference = true; }
                if (reference != r.pixels) throw std::runtime_error("pixel mismatch: scheduling changed the result");
                const auto& s = r.snapshot;
                times[index].push_back(s.wallMs); cpus[index].push_back(s.cpuMs);
                raw << std::fixed << std::setprecision(4) << repeat << ',' << ModeName(item.mode) << ',' << item.workers << ','
                    << c.width << ',' << c.height << ',' << c.samples << ',' << c.bounces << ',' << c.tile << ','
                    << s.wallMs << ',' << s.cpuMs << ',' << s.cpuMs/s.wallMs << ',' << r.checksum << '\n';
                std::cout << "repeat " << repeat+1 << " " << ModeName(item.mode) << " " << item.workers << " workers: "
                    << std::fixed << std::setprecision(2) << s.wallMs << " ms, avg CPU parallelism " << s.cpuMs/s.wallMs << '\n';
                if (repeat == repeats-1) {
                    const std::string stem = std::string(ModeName(item.mode)) + "-" + std::to_string(item.workers);
                    SaveReport(r, out/(stem+".json")); SaveTrace(r, out/(stem+".trace.json"));
                    if (item.mode == Mode::Serial) SaveImage(r, out/"render.bmp");
                }
            }
        }
        auto median = [](std::vector<double> v) { std::sort(v.begin(),v.end()); size_t n = v.size(); return (v[n/2]+v[(n-1)/2])*0.5; };
        std::ofstream summary(out/"summary.csv"); summary.exceptions(std::ios::failbit | std::ios::badbit);
        summary << "mode,workers,median_ms,min_ms,max_ms,speedup,efficiency,median_cpu_ms\n";
        const double baseline = median(times[0]);
        std::cout << "\nmode     workers  median_ms  speedup  efficiency\n";
        for (size_t i = 0; i < cases.size(); ++i) {
            const double ms = median(times[i]), speedup = baseline/ms;
            summary << std::fixed << std::setprecision(4) << ModeName(cases[i].mode) << ',' << cases[i].workers << ',' << ms << ','
                << *std::min_element(times[i].begin(),times[i].end()) << ',' << *std::max_element(times[i].begin(),times[i].end()) << ','
                << speedup << ',' << speedup/cases[i].workers << ',' << median(cpus[i]) << '\n';
            std::cout << std::setw(8) << ModeName(cases[i].mode) << std::setw(8) << cases[i].workers << std::setw(11) << ms
                << std::setw(9) << speedup << std::setw(12) << speedup/cases[i].workers << '\n';
        }
        std::cout << "Every pixel matched the reference. Output: " << out.string() << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << '\n'; return 1; }
}
