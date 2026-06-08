#include "khora/maelstrom/maelstrom.hpp"

#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Direct3D 11 DirectCompute backend. Everything Win32/D3D is confined here.
// ---------------------------------------------------------------------------
#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace khora::maelstrom {
namespace {

// A glyph is kGlyphWords uint64 words; the GPU consumes them as uint32 so
// it can use the countbits() intrinsic. Layout is a straight reinterpret —
// popcount over the 32-bit halves equals popcount over the 64-bit words.
constexpr UINT kWords32     = static_cast<UINT>(lattice::kGlyphWords) * 2; // 314
constexpr UINT kThreads     = 256;
// Guard against asking D3D11 for an impossible single allocation (~1.7 GB
// of structured buffer ≈ 1.4 M glyphs). Beyond this we'd need to tile.
constexpr std::size_t kMaxDbBytes = 1700ull * 1024 * 1024;

// The resonance kernel: one thread per charged glyph, each computing the
// full hamming distance to the probe via 32-bit popcounts.
const char* kKernel = R"HLSL(
StructuredBuffer<uint>   gDB    : register(t0);
StructuredBuffer<uint>   gQuery : register(t1);
RWStructuredBuffer<uint> gOut   : register(u0);

cbuffer Params : register(b0) {
    uint gCount;
    uint gWords;
    uint2 gPad;
};

[numthreads(256, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    uint i = tid.x;
    if (i >= gCount) return;
    uint base = i * gWords;
    uint dist = 0;
    [loop] for (uint w = 0; w < gWords; ++w) {
        dist += countbits(gDB[base + w] ^ gQuery[w]);
    }
    gOut[i] = dist;
}
)HLSL";

std::string narrow(const wchar_t* w) {
    if (!w) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace

struct Maelstrom::Impl {
    ComPtr<ID3D11Device>           device;
    ComPtr<ID3D11DeviceContext>    ctx;
    ComPtr<ID3D11ComputeShader>    shader;

    ComPtr<ID3D11Buffer>              dbBuf;
    ComPtr<ID3D11ShaderResourceView>  dbSRV;
    ComPtr<ID3D11Buffer>              queryBuf;
    ComPtr<ID3D11ShaderResourceView>  querySRV;
    ComPtr<ID3D11Buffer>              outBuf;
    ComPtr<ID3D11UnorderedAccessView> outUAV;
    ComPtr<ID3D11Buffer>              stagingBuf;
    ComPtr<ID3D11Buffer>              paramBuf;

    DeviceInfo info;
    UINT       count     = 0;
    std::size_t dbBytes  = 0;
    bool       ready     = false;

    bool create_query_buffer() {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth           = kWords32 * sizeof(UINT);
        bd.Usage               = D3D11_USAGE_DYNAMIC;
        bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(UINT);
        if (FAILED(device->CreateBuffer(&bd, nullptr, &queryBuf))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format              = DXGI_FORMAT_UNKNOWN;
        sv.ViewDimension       = D3D11_SRV_DIMENSION_BUFFEREX;
        sv.BufferEx.FirstElement = 0;
        sv.BufferEx.NumElements  = kWords32;
        return SUCCEEDED(device->CreateShaderResourceView(queryBuf.Get(), &sv, &querySRV));
    }

    bool create_param_buffer() {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16; // {count, words, pad, pad}
        bd.Usage     = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(device->CreateBuffer(&bd, nullptr, &paramBuf));
    }

    // Run the kernel for `probe` and read back the full distance vector.
    bool dispatch(const lattice::Glyph& probe, std::vector<std::uint32_t>& out);
};

Maelstrom::Maelstrom() : impl_(std::make_unique<Impl>()) {}
Maelstrom::~Maelstrom() = default;
Maelstrom::Maelstrom(Maelstrom&&) noexcept = default;
Maelstrom& Maelstrom::operator=(Maelstrom&&) noexcept = default;

bool Maelstrom::ignite() {
    if (impl_->ready) return true;
    DeviceInfo& info = impl_->info;
    info = DeviceInfo{};

    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        want, 2, D3D11_SDK_VERSION,
        &impl_->device, &got, &impl_->ctx);
    if (FAILED(hr)) {
        info.note = "no Direct3D 11 hardware device (hr=0x" + std::to_string((unsigned)hr) + ")";
        return false;
    }
    info.feature = (got == D3D_FEATURE_LEVEL_11_1) ? "11_1" : "11_0";

    // Adapter identity + VRAM via DXGI.
    ComPtr<IDXGIDevice> dxgiDev;
    if (SUCCEEDED(impl_->device.As(&dxgiDev))) {
        ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(dxgiDev->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                info.adapter = narrow(desc.Description);
                info.vram_mb = static_cast<std::size_t>(desc.DedicatedVideoMemory / (1024 * 1024));
            }
        }
    }

    // Compile + create the resonance kernel.
    ComPtr<ID3DBlob> code, err;
    hr = D3DCompile(kKernel, std::strlen(kKernel), "maelstrom.hlsl",
                    nullptr, nullptr, "CSMain", "cs_5_0",
                    0, 0, &code, &err);
    if (FAILED(hr)) {
        info.note = "kernel compile failed";
        if (err) info.note += std::string(": ") + static_cast<const char*>(err->GetBufferPointer());
        return false;
    }
    if (FAILED(impl_->device->CreateComputeShader(
            code->GetBufferPointer(), code->GetBufferSize(), nullptr, &impl_->shader))) {
        info.note = "could not create compute shader";
        return false;
    }

    if (!impl_->create_query_buffer() || !impl_->create_param_buffer()) {
        info.note = "could not create constant/query buffers";
        return false;
    }

    info.available = true;
    impl_->ready   = true;
    return true;
}

bool Maelstrom::ready() const noexcept { return impl_->ready; }
const DeviceInfo& Maelstrom::device() const noexcept { return impl_->info; }
std::size_t Maelstrom::charged() const noexcept { return impl_->count; }
std::size_t Maelstrom::vram_bytes() const noexcept { return impl_->dbBytes; }

bool Maelstrom::charge(const std::vector<lattice::Glyph>& glyphs) {
    if (!impl_->ready) return false;

    // Drop any previous charge.
    impl_->dbSRV.Reset();
    impl_->dbBuf.Reset();
    impl_->outUAV.Reset();
    impl_->outBuf.Reset();
    impl_->stagingBuf.Reset();
    impl_->count   = 0;
    impl_->dbBytes = 0;

    const UINT n = static_cast<UINT>(glyphs.size());
    if (n == 0) return true;

    const std::size_t dbBytes = static_cast<std::size_t>(n) * kWords32 * sizeof(UINT);
    if (dbBytes > kMaxDbBytes) {
        impl_->info.note = "database exceeds single-buffer limit (" +
                           std::to_string(dbBytes / (1024 * 1024)) + " MB); tiling not yet implemented";
        return false;
    }

    // Pack glyphs contiguously: a straight memcpy of each storage array.
    std::vector<UINT> packed(static_cast<std::size_t>(n) * kWords32);
    for (UINT i = 0; i < n; ++i) {
        std::memcpy(&packed[static_cast<std::size_t>(i) * kWords32],
                    glyphs[i].words().data(),
                    lattice::kGlyphWords * sizeof(lattice::Glyph::Word));
    }

    // DB structured buffer (immutable for the charge's lifetime).
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth           = static_cast<UINT>(dbBytes);
        bd.Usage               = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(UINT);
        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = packed.data();
        if (FAILED(impl_->device->CreateBuffer(&bd, &init, &impl_->dbBuf))) {
            impl_->info.note = "VRAM upload (DB buffer) failed";
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format                = DXGI_FORMAT_UNKNOWN;
        sv.ViewDimension         = D3D11_SRV_DIMENSION_BUFFEREX;
        sv.BufferEx.FirstElement = 0;
        sv.BufferEx.NumElements  = n * kWords32;
        if (FAILED(impl_->device->CreateShaderResourceView(impl_->dbBuf.Get(), &sv, &impl_->dbSRV))) {
            impl_->info.note = "DB SRV failed";
            return false;
        }
    }

    // Output UAV buffer (one uint per glyph) + CPU-readable staging copy.
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth           = n * sizeof(UINT);
        bd.Usage               = D3D11_USAGE_DEFAULT;
        bd.BindFlags           = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(UINT);
        if (FAILED(impl_->device->CreateBuffer(&bd, nullptr, &impl_->outBuf))) {
            impl_->info.note = "output buffer failed";
            return false;
        }
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.Format              = DXGI_FORMAT_UNKNOWN;
        uv.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uv.Buffer.FirstElement = 0;
        uv.Buffer.NumElements  = n;
        if (FAILED(impl_->device->CreateUnorderedAccessView(impl_->outBuf.Get(), &uv, &impl_->outUAV))) {
            impl_->info.note = "output UAV failed";
            return false;
        }

        D3D11_BUFFER_DESC sd{};
        sd.ByteWidth      = n * sizeof(UINT);
        sd.Usage          = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(impl_->device->CreateBuffer(&sd, nullptr, &impl_->stagingBuf))) {
            impl_->info.note = "staging buffer failed";
            return false;
        }
    }

    // Update params (count, words).
    UINT params[4] = { n, kWords32, 0, 0 };
    impl_->ctx->UpdateSubresource(impl_->paramBuf.Get(), 0, nullptr, params, 0, 0);

    impl_->count   = n;
    impl_->dbBytes = dbBytes;
    return true;
}

// Runs the kernel for `probe` and reads back the full distance vector.
// Returns false on any pipeline failure.
bool Maelstrom::Impl::dispatch(const lattice::Glyph& probe, std::vector<std::uint32_t>& out) {
    if (!ready || count == 0) return false;

    // Upload the probe into the dynamic query buffer.
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(queryBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    std::memcpy(m.pData, probe.words().data(),
                lattice::kGlyphWords * sizeof(lattice::Glyph::Word));
    ctx->Unmap(queryBuf.Get(), 0);

    // Bind the pipeline.
    ID3D11ShaderResourceView* srvs[2] = { dbSRV.Get(), querySRV.Get() };
    ID3D11UnorderedAccessView* uav    = outUAV.Get();
    ID3D11Buffer* cb                  = paramBuf.Get();
    ctx->CSSetShader(shader.Get(), nullptr, 0);
    ctx->CSSetShaderResources(0, 2, srvs);
    ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    ctx->CSSetConstantBuffers(0, 1, &cb);

    const UINT groups = (count + kThreads - 1) / kThreads;
    ctx->Dispatch(groups, 1, 1);

    // Unbind the UAV so the next charge/dispatch is clean.
    ID3D11UnorderedAccessView* nullUAV = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

    // Read back the distances.
    ctx->CopyResource(stagingBuf.Get(), outBuf.Get());
    D3D11_MAPPED_SUBRESOURCE r{};
    if (FAILED(ctx->Map(stagingBuf.Get(), 0, D3D11_MAP_READ, 0, &r))) return false;
    out.resize(count);
    std::memcpy(out.data(), r.pData, static_cast<std::size_t>(count) * sizeof(std::uint32_t));
    ctx->Unmap(stagingBuf.Get(), 0);
    return true;
}

std::vector<std::uint32_t> Maelstrom::hamming_all(const lattice::Glyph& probe) const {
    std::vector<std::uint32_t> out;
    if (!impl_->dispatch(probe, out)) out.clear();
    return out;
}

std::vector<Neighbour> Maelstrom::resonate(const lattice::Glyph& probe, std::size_t k) const {
    std::vector<std::uint32_t> dist;
    if (!impl_->dispatch(probe, dist)) return {};

    const std::size_t n = dist.size();
    k = std::min(k, n);
    std::vector<std::uint32_t> idx(n);
    for (std::uint32_t i = 0; i < n; ++i) idx[i] = i;
    std::partial_sort(idx.begin(), idx.begin() + k, idx.end(),
                      [&](std::uint32_t a, std::uint32_t b) {
                          if (dist[a] != dist[b]) return dist[a] < dist[b];
                          return a < b;
                      });
    std::vector<Neighbour> out;
    out.reserve(k);
    for (std::size_t i = 0; i < k; ++i) out.push_back({ idx[i], dist[idx[i]] });
    return out;
}

} // namespace khora::maelstrom

#else // !_WIN32 — no DirectCompute; the Maelstrom never ignites.

namespace khora::maelstrom {

struct Maelstrom::Impl { DeviceInfo info; };

Maelstrom::Maelstrom() : impl_(std::make_unique<Impl>()) {}
Maelstrom::~Maelstrom() = default;
Maelstrom::Maelstrom(Maelstrom&&) noexcept = default;
Maelstrom& Maelstrom::operator=(Maelstrom&&) noexcept = default;

bool Maelstrom::ignite() { impl_->info.note = "DirectCompute is Windows-only"; return false; }
bool Maelstrom::ready() const noexcept { return false; }
const DeviceInfo& Maelstrom::device() const noexcept { return impl_->info; }
bool Maelstrom::charge(const std::vector<lattice::Glyph>&) { return false; }
std::size_t Maelstrom::charged() const noexcept { return 0; }
std::size_t Maelstrom::vram_bytes() const noexcept { return 0; }
std::vector<Neighbour> Maelstrom::resonate(const lattice::Glyph&, std::size_t) const { return {}; }
std::vector<std::uint32_t> Maelstrom::hamming_all(const lattice::Glyph&) const { return {}; }

} // namespace khora::maelstrom

#endif
