#include "classroom.h"
namespace classroom {
std::vector<Particle> InitialParticles(uint32_t count) {
    std::vector<Particle> result(count);
    uint32_t state = 0x12345678;
    auto random = [&] {
        state = 1664525u * state + 1013904223u;
        return static_cast<float>((state >> 8) & 0xFFFFFF) / 16777216.0f;
    };
    for (auto &p : result) {
        p.x = random();
        p.y = random();
        p.vx = (random() - 0.5f) * 0.15f;
        p.vy = (random() - 0.5f) * 0.15f;
    }
    return result;
}
void UpdateParticles(std::vector<Particle> &data, size_t first, size_t last, float dt) {
    for (size_t i = first; i < last; ++i) {
        auto &p = data[i];
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.x >= 1)
            p.x -= 1;
        if (p.y >= 1)
            p.y -= 1;
        if (p.x < 0)
            p.x += 1;
        if (p.y < 0)
            p.y += 1;
    }
}
void VerifyParticles(const std::vector<Particle> &actual, const std::vector<Particle> &expected) {
    if (actual.size() != expected.size())
        throw std::runtime_error("Particle count mismatch");
    float maxError = 0;
    for (size_t i = 0; i < actual.size(); ++i) {
        const auto &a = actual[i];
        const auto &b = expected[i];
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || a.x < 0 || a.x >= 1 || a.y < 0 ||
            a.y >= 1)
            throw std::runtime_error("Particle outside valid range or non-finite");
        auto periodic = [](float x, float y) {
            float e = std::abs(x - y);
            return std::min(e, 1.0f - e);
        };
        maxError = std::max({maxError, periodic(a.x, b.x), periodic(a.y, b.y),
                             std::abs(a.vx - b.vx), std::abs(a.vy - b.vy)});
    }
    if (maxError > 0.0002f)
        throw std::runtime_error("CPU/GPU particle reference mismatch: " +
                                 std::to_string(maxError));
    std::cout << "Particle reference PASS, max error=" << maxError << '\n';
}
void ParticleRenderer::Init(Device &gpu, uint32_t capacity) {
    capacity_ = capacity;
    auto cpu = gpu.Compile(L"particles.hlsl", "CpuVS", "vs_5_0");
    auto shader = gpu.Compile(L"particles.hlsl", "GpuVS", "vs_5_0");
    auto pixel = gpu.Compile(L"particles.hlsl", "PS", "ps_5_0");
    Check(gpu.device->CreateVertexShader(cpu->GetBufferPointer(), cpu->GetBufferSize(), nullptr,
                                         cpuVS_.ReleaseAndGetAddressOf()),
          "CPU VS");
    Check(gpu.device->CreateVertexShader(shader->GetBufferPointer(), shader->GetBufferSize(),
                                         nullptr, gpuVS_.ReleaseAndGetAddressOf()),
          "GPU VS");
    Check(gpu.device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr,
                                        pixel_.ReleaseAndGetAddressOf()),
          "Pixel shader");
    D3D11_INPUT_ELEMENT_DESC element{
        "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    Check(gpu.device->CreateInputLayout(&element, 1, cpu->GetBufferPointer(), cpu->GetBufferSize(),
                                        layout_.ReleaseAndGetAddressOf()),
          "Input layout");
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = capacity * sizeof(Particle);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Check(gpu.device->CreateBuffer(&desc, nullptr, dynamic_.ReleaseAndGetAddressOf()),
          "Dynamic particle buffer");
    desc.ByteWidth = sizeof(Constants);
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Check(gpu.device->CreateBuffer(&desc, nullptr, constants_.ReleaseAndGetAddressOf()),
          "Constants");
    ConstantsFor(gpu, capacity);
}
void ParticleRenderer::ConstantsFor(Device &gpu, uint32_t count, float dt, uint32_t grid) {
    Constants value{count, dt, 0, grid};
    D3D11_MAPPED_SUBRESOURCE map{};
    Check(gpu.context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map constants");
    memcpy(map.pData, &value, sizeof(value));
    gpu.context->Unmap(constants_.Get(), 0);
}
void ParticleRenderer::Cpu(Device &gpu, const std::vector<Particle> &particles, Metrics &m,
                           bool stream, bool batched) {
    if (particles.size() > capacity_)
        throw std::runtime_error("Dynamic buffer capacity exceeded");
    auto *ctx = gpu.context.Get();
    ctx->IASetInputLayout(layout_.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    UINT stride = sizeof(Particle), offset = 0;
    auto *vb = dynamic_.Get();
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->VSSetShader(cpuVS_.Get(), nullptr, 0);
    ctx->PSSetShader(pixel_.Get(), nullptr, 0);
    const UINT total = static_cast<UINT>(particles.size()),
               batch = batched ? 1024u : std::max(1u, total);
    for (UINT first = 0; first < total; first += batch) {
        UINT n = std::min(batch, total - first), destination = stream ? first : 0;
        auto start = Clock::now();
        D3D11_MAPPED_SUBRESOURCE map{};
        // DISCARD 从新逻辑内容开始；NO_OVERWRITE 承诺不写 GPU 正在读取的区间。
        D3D11_MAP kind =
            (stream && first > 0) ? D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD;
        Check(ctx->Map(dynamic_.Get(), 0, kind, 0, &map), "Map particle upload");
        memcpy(static_cast<Particle *>(map.pData) + destination, particles.data() + first,
               static_cast<size_t>(n) * sizeof(Particle));
        ctx->Unmap(dynamic_.Get(), 0);
        m.uploadMs += Ms(start);
        if (gpu.drawTimer && !batched)
            gpu.drawTimer->BeginDraw(gpu);
        ctx->Draw(n, destination);
        if (gpu.drawTimer && !batched)
            gpu.drawTimer->EndDraw(gpu);
        ++m.drawCalls;
    }
}
void ParticleRenderer::Gpu(Device &gpu, ID3D11ShaderResourceView *srv, uint32_t count, Metrics &m) {
    auto *ctx = gpu.context.Get();
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetShader(gpuVS_.Get(), nullptr, 0);
    ctx->PSSetShader(pixel_.Get(), nullptr, 0);
    ctx->VSSetShaderResources(0, 1, &srv);
    ctx->Draw(count, 0);
    ++m.drawCalls;
    ID3D11ShaderResourceView *nullSRV = nullptr;
    ctx->VSSetShaderResources(0, 1, &nullSRV);
}
void ParticleRenderer::GridState(ID3D11DeviceContext *ctx) {
    ctx->IASetInputLayout(layout_.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(cpuVS_.Get(), nullptr, 0);
    ctx->PSSetShader(pixel_.Get(), nullptr, 0);
    auto *vb = gridVertices_.Get();
    UINT stride = sizeof(Particle), offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
}
void ParticleRenderer::InitGrid(Device &gpu, uint32_t draws) {
    const UINT side = static_cast<UINT>(std::ceil(std::sqrt(static_cast<double>(draws))));
    const float size = static_cast<float>(side);
    std::vector<Particle> vertices(static_cast<size_t>(draws) * 3);
    const float corners[3][2] = {{-0.75f, -0.65f}, {0, 0.8f}, {0.75f, -0.65f}};
    for (UINT cell = 0; cell < draws; ++cell)
        for (UINT corner = 0; corner < 3; ++corner) {
            vertices[static_cast<size_t>(cell) * 3 + corner] = {
                (static_cast<float>(cell % side) + 0.5f + corners[corner][0] * 0.48f) / size,
                (static_cast<float>(cell / side) + 0.5f + corners[corner][1] * 0.48f) / size, 0, 0};
        }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(Particle));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{vertices.data(), 0, 0};
    Check(gpu.device->CreateBuffer(&bd, &data, gridVertices_.ReleaseAndGetAddressOf()),
          "Grid vertex buffer");
}
void ParticleRenderer::Grid(Device &gpu, uint32_t draws, Metrics &m) {
    GridState(gpu.context.Get());
    for (UINT i = 0; i < draws; ++i)
        gpu.context->Draw(3, i * 3);
    m.drawCalls += draws;
}
void ComputeParticles::Init(Device &gpu, uint32_t n) {
    count = n;
    auto initial = InitialParticles(count);
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = count * sizeof(Particle);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Particle);
    D3D11_SUBRESOURCE_DATA data{initial.data(), 0, 0};
    Check(gpu.device->CreateBuffer(&bd, &data, buffer.ReleaseAndGetAddressOf()),
          "Structured particles");
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = count;
    Check(gpu.device->CreateShaderResourceView(buffer.Get(), &sd, srv.ReleaseAndGetAddressOf()),
          "Particle SRV");
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = count;
    Check(gpu.device->CreateUnorderedAccessView(buffer.Get(), &ud, uav.ReleaseAndGetAddressOf()),
          "Particle UAV");
    auto code = gpu.Compile(L"particles.hlsl", "UpdateCS", "cs_5_0");
    Check(gpu.device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                          shader.ReleaseAndGetAddressOf()),
          "Compute shader");
}
void ComputeParticles::Step(Device &gpu, ParticleRenderer &renderer, float dt) {
    renderer.ConstantsFor(gpu, count, dt);
    auto *ctx = gpu.context.Get();
    auto *cb = renderer.ConstantBuffer();
    auto *output = uav.Get();
    ctx->CSSetConstantBuffers(0, 1, &cb);
    ctx->CSSetShader(shader.Get(), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    ctx->Dispatch((count + 255) / 256, 1, 1);
    // 解除 UAV 绑定后才把同一资源绑定为 VS SRV。这里没有 CPU 等待。
    ID3D11UnorderedAccessView *nullUAV = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
}
std::vector<Particle> ComputeParticles::ReadBlocking(Device &gpu) {
    D3D11_BUFFER_DESC bd{};
    buffer->GetDesc(&bd);
    bd.Usage = D3D11_USAGE_STAGING;
    bd.BindFlags = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bd.MiscFlags = 0;
    bd.StructureByteStride = 0;
    ComPtr<ID3D11Buffer> staging;
    Check(gpu.device->CreateBuffer(&bd, nullptr, &staging), "Validation staging");
    gpu.context->CopyResource(staging.Get(), buffer.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    Check(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "Blocking readback Map");
    std::vector<Particle> result(count);
    memcpy(result.data(), map.pData, static_cast<size_t>(count) * sizeof(Particle));
    gpu.context->Unmap(staging.Get(), 0);
    return result;
}
void ReadbackRing::Init(Device &gpu, ID3D11Buffer *source, uint32_t count) {
    count_ = count;
    latest.clear();
    consumed = 0;
    latestFrame = 0;
    D3D11_BUFFER_DESC bd{};
    source->GetDesc(&bd);
    bd.Usage = D3D11_USAGE_STAGING;
    bd.BindFlags = 0;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    bd.MiscFlags = 0;
    bd.StructureByteStride = 0;
    D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    for (auto &s : slots_) {
        s.pending = false;
        Check(gpu.device->CreateBuffer(&bd, nullptr, s.staging.ReleaseAndGetAddressOf()),
              "Ring staging");
        Check(gpu.device->CreateQuery(&qd, s.complete.ReleaseAndGetAddressOf()), "Ring event");
    }
}
void ReadbackRing::Enqueue(Device &gpu, ID3D11Buffer *source, uint64_t frame, Metrics &m) {
    for (auto &s : slots_)
        if (!s.pending) {
            gpu.context->CopyResource(s.staging.Get(), source);
            gpu.context->End(s.complete.Get());
            s.frame = frame;
            s.pending = true;
            return;
        }
    ++m.droppedReadbacks; // 满环时跳过新采样，不覆盖尚未完成的数据，也不强制等待。
}
void ReadbackRing::Poll(Device &gpu, uint64_t frame, Metrics &m) {
    auto start = Clock::now();
    for (auto &s : slots_)
        if (s.pending) {
            HRESULT hr =
                gpu.context->GetData(s.complete.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
            Check(hr, "Readback completion query");
            if (hr != S_OK)
                continue;
            D3D11_MAPPED_SUBRESOURCE map{};
            hr = gpu.context->Map(s.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT,
                                  &map);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
                continue;
            Check(hr, "Ready readback Map");
            if (latest.empty() || s.frame >= latestFrame) {
                latest.resize(count_);
                memcpy(latest.data(), map.pData, static_cast<size_t>(count_) * sizeof(Particle));
                latestFrame = s.frame;
            }
            gpu.context->Unmap(s.staging.Get(), 0);
            s.pending = false;
            ++consumed;
        }
    m.readbackMs += Ms(start);
    if (!latest.empty())
        m.readbackAge = static_cast<int64_t>(frame - latestFrame);
}
} // namespace classroom
