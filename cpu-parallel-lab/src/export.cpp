#include "engine.h"
#include <windows.h>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace lab {
static std::ofstream Open(const std::filesystem::path& path, bool binary = false) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, binary ? std::ios::binary : std::ios::out);
    f.exceptions(std::ios::badbit | std::ios::failbit);
    return f;
}
void SaveImage(const Result& r, const std::filesystem::path& path) {
    const auto& c = r.snapshot.config;
    auto f = Open(path, true);
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file)+sizeof(info);
    file.bfSize = file.bfOffBits + DWORD(r.pixels.size()*4);
    info.biSize = sizeof(info); info.biWidth = c.width; info.biHeight = -c.height;
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    f.write(reinterpret_cast<const char*>(&file), sizeof(file));
    f.write(reinterpret_cast<const char*>(&info), sizeof(info));
    f.write(reinterpret_cast<const char*>(r.pixels.data()), std::streamsize(r.pixels.size()*4));
}
void SaveTrace(const Result& r, const std::filesystem::path& path) {
    auto f = Open(path);
    f << std::fixed << std::setprecision(3) << "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
    bool comma = false;
    for (const auto& t : r.snapshot.tiles) {
        if (t.state != 2 && t.state != 3) continue;
        if (comma) f << ',';
        comma = true;
        f << "{\"name\":\"tile " << t.x << ',' << t.y << "\",\"cat\":\"CPU ray tracing\",\"ph\":\"X\",\"pid\":1,\"tid\":"
          << t.owner << ",\"ts\":" << t.begin*1000 << ",\"dur\":" << (t.end-t.begin)*1000 << '}';
    }
    f << "]}";
}
void SaveReport(const Result& r, const std::filesystem::path& path) {
    const auto& s = r.snapshot; const auto& c = s.config;
    auto f = Open(path);
    f << std::fixed << std::setprecision(4)
      << "{\n  \"mode\":\"" << ModeName(c.mode) << "\",\n  \"workers\":" << s.workers
      << ",\n  \"width\":" << c.width << ",\n  \"height\":" << c.height << ",\n  \"samples\":" << c.samples
      << ",\n  \"bounces\":" << c.bounces << ",\n  \"tile\":" << c.tile
      << ",\n  \"completed\":" << s.completed << ",\n  \"total\":" << s.total
      << ",\n  \"cancelled\":" << (s.cancelled ? "true" : "false")
      << ",\n  \"wall_ms\":" << s.wallMs << ",\n  \"worker_cpu_ms\":" << s.cpuMs
      << ",\n  \"average_parallelism\":" << (s.wallMs > 0 ? s.cpuMs/s.wallMs : 0)
      << ",\n  \"checksum\":\"" << r.checksum << "\",\n  \"worker_stats\":[";
    for (size_t i = 0; i < s.stats.size(); ++i) {
        if (i) f << ',';
        f << "{\"worker\":" << i << ",\"cpu_ms\":" << s.stats[i].cpuMs << ",\"tiles\":" << s.stats[i].tiles << '}';
    }
    f << "]\n}\n";
}
} // namespace lab
