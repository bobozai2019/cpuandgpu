#pragma once
#include <windows.h>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace classroom {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
inline double Ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void Check(HRESULT hr, const char *operation);
std::wstring Wide(const std::string &text);
std::string Utf8(const std::wstring &text);
struct Options {
    bool headless = false, warp = false, debug = false, validate = false, vsync = true;
    uint32_t frames = 0, count = 65536, draws = 4096, workers = 4, mode = 0;
    uint32_t cpuMs = 0, gpuRepeats = 1;
    std::filesystem::path output, capture;
};
struct Metrics {
    uint64_t frame = 0;
    double cpuMs = 0, presentMs = 0, updateMs = 0, uploadMs = 0, readbackMs = 0;
    double gpuMs = -1, gpuDrawMs = -1;
    int64_t gpuFrame = -1, readbackAge = -1;
    uint32_t drawCalls = 0, droppedReadbacks = 0;
};
struct LessonInfo {
    std::wstring title, goal, concepts, experiment;
    std::vector<std::wstring> modes;
    std::string document;
};
class GpuTimer;
class Device {
  public:
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapchain;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11Texture2D> swapBuffer;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11InfoQueue> infoQueue;
    std::wstring adapter;
    bool debugActive = false;
    GpuTimer *drawTimer = nullptr; // Optional single-draw measurement (chapter 04).
    UINT width = 800, height = 640;
    void Init(HWND viewport, const Options &options);
    void Begin();
    void BindTarget(ID3D11DeviceContext *ctx);
    void Present(bool vsync);
    void WaitIdle(); // 仅用于退出、验证、或明确的“阻塞”对照实验。
    void Capture(const std::filesystem::path &path);
    void ValidateTarget();
    uint32_t DebugErrors();
    ComPtr<ID3DBlob> Compile(const wchar_t *file, const char *entry, const char *profile);
};
class GpuTimer {
    struct Slot {
        ComPtr<ID3D11Query> begin, end, disjoint, drawBegin, drawEnd;
        bool drawStarted = false, drawFinished = false;
        uint64_t frame = 0;
        bool pending = false;
    };
    std::array<Slot, 16> slots_;
    Slot *active_ = nullptr;

  public:
    void Init(Device &gpu);
    void Poll(Device &gpu, const std::function<void(uint64_t, double)> &consume,
              const std::function<void(uint64_t, double)> &consumeDraw = {});
    void BeginDraw(Device &gpu);
    void EndDraw(Device &gpu);
    void Begin(Device &gpu, uint64_t frame);
    void End(Device &gpu);
    uint32_t dropped = 0;
};
// 固定线程池。一次 Run 对所有 worker 派发一个任务；每帧不重新创建线程。
class Jobs {
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable work_, done_;
    std::function<void(uint32_t)> task_;
    uint64_t generation_ = 0;
    uint32_t remaining_ = 0;
    bool stop_ = false;
    std::exception_ptr error_;

  public:
    explicit Jobs(uint32_t count);
    ~Jobs();
    void Run(const std::function<void(uint32_t)> &task);
    uint32_t Size() const {
        return static_cast<uint32_t>(threads_.size());
    }
};
struct Particle {
    float x, y, vx, vy;
};
static_assert(sizeof(Particle) == 16, "Must match the HLSL structured buffer stride");
std::vector<Particle> InitialParticles(uint32_t count);
void UpdateParticles(std::vector<Particle> &data, size_t first, size_t last, float dt);
void VerifyParticles(const std::vector<Particle> &actual, const std::vector<Particle> &expected);
struct alignas(16) Constants {
    uint32_t count;
    float dt;
    float time;
    uint32_t grid;
};
class ParticleRenderer {
    ComPtr<ID3D11VertexShader> cpuVS_, gpuVS_;
    ComPtr<ID3D11PixelShader> pixel_;
    ComPtr<ID3D11InputLayout> layout_;
    ComPtr<ID3D11Buffer> dynamic_, constants_, gridVertices_;
    uint32_t capacity_ = 0;

  public:
    void Init(Device &gpu, uint32_t capacity);
    void ConstantsFor(Device &gpu, uint32_t count, float dt = 1.0f / 60.0f, uint32_t grid = 64);
    void Cpu(Device &gpu, const std::vector<Particle> &particles, Metrics &m, bool stream = false,
             bool batched = false);
    void Gpu(Device &gpu, ID3D11ShaderResourceView *srv, uint32_t count, Metrics &m);
    void GridState(ID3D11DeviceContext *context);
    void InitGrid(Device &gpu, uint32_t draws);
    void Grid(Device &gpu, uint32_t draws, Metrics &m);
    ID3D11Buffer *ConstantBuffer() const {
        return constants_.Get();
    }
};
class ComputeParticles {
  public:
    ComPtr<ID3D11Buffer> buffer;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11ComputeShader> shader;
    uint32_t count = 0;
    void Init(Device &gpu, uint32_t n);
    void Step(Device &gpu, ParticleRenderer &renderer, float dt);
    std::vector<Particle> ReadBlocking(Device &gpu);
};
class ReadbackRing {
    struct Slot {
        ComPtr<ID3D11Buffer> staging;
        ComPtr<ID3D11Query> complete;
        uint64_t frame = 0;
        bool pending = false;
    };
    std::array<Slot, 4> slots_;
    uint32_t count_ = 0;

  public:
    std::vector<Particle> latest;
    uint64_t latestFrame = 0, consumed = 0;
    void Init(Device &gpu, ID3D11Buffer *source, uint32_t count);
    void Enqueue(Device &gpu, ID3D11Buffer *source, uint64_t frame, Metrics &m);
    void Poll(Device &gpu, uint64_t frame, Metrics &m);
};
class Lesson {
  public:
    virtual ~Lesson() = default;
    virtual LessonInfo Info() const = 0;
    virtual void Init(Device &, const Options &) = 0;
    virtual void Frame(Device &, const Options &, Metrics &, float dt) = 0;
    virtual void Validate(Device &, const Options &, uint64_t frames) {
        (void)frames;
    }
};
int Run(int argc, char **argv, std::unique_ptr<Lesson> lesson);
} // namespace classroom
