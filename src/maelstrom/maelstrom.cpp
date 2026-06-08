#include "khora/maelstrom/maelstrom.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

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
constexpr UINT kMaxK        = 64;  // largest k a single resonate() will honour
// Guard against asking D3D11 for an impossible single allocation (~1.7 GB
// of structured buffer ≈ 1.4 M glyphs). Beyond this we'd need to tile.
constexpr std::size_t kMaxDbBytes = 1700ull * 1024 * 1024;

// The resonance kernel. One thread per charged glyph computes the full
// 10,000-bit Hamming distance to the probe (32-bit popcounts), writes it to
// gOut (the audit channel), then the 256-thread group cooperatively reduces
// its slice to the k nearest in groupshared memory. Only k candidates per
// group ever leave VRAM — the host merges groups*k of them, never all N.
const char* kKernel = R"HLSL(
StructuredBuffer<uint>    gDB    : register(t0);
StructuredBuffer<uint>    gQuery : register(t1);
RWStructuredBuffer<uint>  gOut   : register(u0);  // per-glyph distance (audit)
RWStructuredBuffer<uint2> gCand  : register(u1);  // per-group (dist, index) * K

cbuffer Params : register(b0) {
    uint gCount;
    uint gWords;
    uint gK;
    uint gGroups;
};

groupshared uint sDist[256];
groupshared uint sIdx[256];

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID,
            uint3 gtid : SV_GroupThreadID,
            uint3 gid  : SV_GroupID) {
    uint i = dtid.x;
    uint t = gtid.x;

    uint dist = 0xFFFFFFFFu; // sentinel for out-of-range lanes
    if (i < gCount) {
        uint base = i * gWords;
        uint d = 0;
        [loop] for (uint w = 0; w < gWords; ++w) {
            d += countbits(gDB[base + w] ^ gQuery[w]);
        }
        dist = d;
        gOut[i] = d;
    }
    sDist[t] = dist;
    sIdx[t]  = i;
    GroupMemoryBarrierWithGroupSync();

    // Thread 0 extracts the k smallest of this group's 256 lanes. Keeping
    // exactly k per group is provably sufficient: an element in the global
    // top-k has < k elements smaller than it overall, hence < k within its
    // own group, so it survives this local selection.
    if (t == 0) {
        uint kk = min(gK, 256u);
        for (uint s = 0; s < kk; ++s) {
            uint bestD = 0xFFFFFFFFu, bestPos = 0;
            for (uint j = 0; j < 256; ++j) {
                if (sDist[j] < bestD) { bestD = sDist[j]; bestPos = j; }
            }
            uint2 c;
            c.x = bestD;
            c.y = (bestD == 0xFFFFFFFFu) ? 0xFFFFFFFFu : sIdx[bestPos];
            gCand[gid.x * gK + s] = c;
            if (bestD != 0xFFFFFFFFu) sDist[bestPos] = 0xFFFFFFFFu; // remove + repeat
        }
    }
}

// Batched variant: gid.y selects the probe, gQ holds Q probes back-to-back,
// gBCand is laid out [probe][group][k]. One dispatch handles every probe.
StructuredBuffer<uint>    gQB    : register(t2);  // Q probes * WORDS
RWStructuredBuffer<uint2> gBCand : register(u2);  // Q * groups * K

groupshared uint bDist[256];
groupshared uint bIdx[256];

[numthreads(256, 1, 1)]
void CSBatch(uint3 dtid : SV_DispatchThreadID,
             uint3 gtid : SV_GroupThreadID,
             uint3 gid  : SV_GroupID) {
    uint i = dtid.x;     // glyph index
    uint p = gid.y;      // probe index
    uint t = gtid.x;

    uint dist = 0xFFFFFFFFu;
    if (i < gCount) {
        uint base  = i * gWords;
        uint qbase = p * gWords;
        uint d = 0;
        [loop] for (uint w = 0; w < gWords; ++w) {
            d += countbits(gDB[base + w] ^ gQB[qbase + w]);
        }
        dist = d;
    }
    bDist[t] = dist;
    bIdx[t]  = i;
    GroupMemoryBarrierWithGroupSync();

    if (t == 0) {
        uint kk = min(gK, 256u);
        uint outBase = (p * gGroups + gid.x) * gK;
        for (uint s = 0; s < kk; ++s) {
            uint bestD = 0xFFFFFFFFu, bestPos = 0;
            for (uint j = 0; j < 256; ++j) {
                if (bDist[j] < bestD) { bestD = bDist[j]; bestPos = j; }
            }
            uint2 c;
            c.x = bestD;
            c.y = (bestD == 0xFFFFFFFFu) ? 0xFFFFFFFFu : bIdx[bestPos];
            gBCand[outBase + s] = c;
            if (bestD != 0xFFFFFFFFu) bDist[bestPos] = 0xFFFFFFFFu;
        }
    }
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
    ComPtr<ID3D11ComputeShader>    shaderBatch;

    ComPtr<ID3D11Buffer>              dbBuf;
    ComPtr<ID3D11ShaderResourceView>  dbSRV;
    ComPtr<ID3D11Buffer>              queryBuf;
    ComPtr<ID3D11ShaderResourceView>  querySRV;
    ComPtr<ID3D11Buffer>              outBuf;      // per-glyph distances (N)
    ComPtr<ID3D11UnorderedAccessView> outUAV;
    ComPtr<ID3D11Buffer>              outStaging;  // CPU read-back of outBuf
    ComPtr<ID3D11Buffer>              candBuf;     // per-group candidates
    ComPtr<ID3D11UnorderedAccessView> candUAV;
    ComPtr<ID3D11Buffer>              candStaging;
    ComPtr<ID3D11Buffer>              paramBuf;

    DeviceInfo  info;
    UINT        count   = 0;
    UINT        groups  = 0;
    std::size_t dbBytes = 0;
    bool        ready   = false;

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
        sv.Format                = DXGI_FORMAT_UNKNOWN;
        sv.ViewDimension         = D3D11_SRV_DIMENSION_BUFFEREX;
        sv.BufferEx.FirstElement = 0;
        sv.BufferEx.NumElements  = kWords32;
        return SUCCEEDED(device->CreateShaderResourceView(queryBuf.Get(), &sv, &querySRV));
    }

    bool create_param_buffer() {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 16; // {count, words, k, groups}
        bd.Usage     = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(device->CreateBuffer(&bd, nullptr, &paramBuf));
    }

    // Upload the probe, set params, dispatch. gOut + gCand are left populated.
    bool run(const lattice::Glyph& probe, UINT k);
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

    // The batched entry point compiles from the same source.
    ComPtr<ID3DBlob> bcode, berr;
    hr = D3DCompile(kKernel, std::strlen(kKernel), "maelstrom.hlsl",
                    nullptr, nullptr, "CSBatch", "cs_5_0", 0, 0, &bcode, &berr);
    if (FAILED(hr)) {
        info.note = "batch kernel compile failed";
        if (berr) info.note += std::string(": ") + static_cast<const char*>(berr->GetBufferPointer());
        return false;
    }
    if (FAILED(impl_->device->CreateComputeShader(
            bcode->GetBufferPointer(), bcode->GetBufferSize(), nullptr, &impl_->shaderBatch))) {
        info.note = "could not create batch compute shader";
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
    impl_->dbSRV.Reset();       impl_->dbBuf.Reset();
    impl_->outUAV.Reset();      impl_->outBuf.Reset();   impl_->outStaging.Reset();
    impl_->candUAV.Reset();     impl_->candBuf.Reset();   impl_->candStaging.Reset();
    impl_->count = impl_->groups = 0;
    impl_->dbBytes = 0;

    const UINT n = static_cast<UINT>(glyphs.size());
    if (n == 0) return true;

    const std::size_t dbBytes = static_cast<std::size_t>(n) * kWords32 * sizeof(UINT);
    if (dbBytes > kMaxDbBytes) {
        impl_->info.note = "database exceeds single-buffer limit (" +
                           std::to_string(dbBytes / (1024 * 1024)) + " MB); tiling not yet implemented";
        return false;
    }
    const UINT groups = (n + kThreads - 1) / kThreads;

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

    // Output distances (one uint per glyph) + staging copy for audits.
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
        if (FAILED(impl_->device->CreateBuffer(&sd, nullptr, &impl_->outStaging))) {
            impl_->info.note = "output staging failed";
            return false;
        }
    }

    // Per-group candidate buffer: groups * kMaxK pairs of (dist, index).
    {
        const UINT candElems = groups * kMaxK;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth           = candElems * 2 * sizeof(UINT); // uint2
        bd.Usage               = D3D11_USAGE_DEFAULT;
        bd.BindFlags           = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = 2 * sizeof(UINT);
        if (FAILED(impl_->device->CreateBuffer(&bd, nullptr, &impl_->candBuf))) {
            impl_->info.note = "candidate buffer failed";
            return false;
        }
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.Format              = DXGI_FORMAT_UNKNOWN;
        uv.ViewDimension       = D3D11_UAV_DIMENSION_BUFFER;
        uv.Buffer.FirstElement = 0;
        uv.Buffer.NumElements  = candElems;
        if (FAILED(impl_->device->CreateUnorderedAccessView(impl_->candBuf.Get(), &uv, &impl_->candUAV))) {
            impl_->info.note = "candidate UAV failed";
            return false;
        }
        D3D11_BUFFER_DESC sd{};
        sd.ByteWidth      = candElems * 2 * sizeof(UINT);
        sd.Usage          = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(impl_->device->CreateBuffer(&sd, nullptr, &impl_->candStaging))) {
            impl_->info.note = "candidate staging failed";
            return false;
        }
    }

    impl_->count   = n;
    impl_->groups  = groups;
    impl_->dbBytes = dbBytes;
    return true;
}

bool Maelstrom::Impl::run(const lattice::Glyph& probe, UINT k) {
    if (!ready || count == 0) return false;
    if (k < 1) k = 1;
    if (k > kMaxK) k = kMaxK;

    // Upload the probe into the dynamic query buffer.
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(queryBuf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    std::memcpy(m.pData, probe.words().data(),
                lattice::kGlyphWords * sizeof(lattice::Glyph::Word));
    ctx->Unmap(queryBuf.Get(), 0);

    UINT params[4] = { count, kWords32, k, groups };
    ctx->UpdateSubresource(paramBuf.Get(), 0, nullptr, params, 0, 0);

    ID3D11ShaderResourceView*  srvs[2] = { dbSRV.Get(), querySRV.Get() };
    ID3D11UnorderedAccessView* uavs[2] = { outUAV.Get(), candUAV.Get() };
    ID3D11Buffer*              cb      = paramBuf.Get();
    ctx->CSSetShader(shader.Get(), nullptr, 0);
    ctx->CSSetShaderResources(0, 2, srvs);
    ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
    ctx->CSSetConstantBuffers(0, 1, &cb);

    ctx->Dispatch(groups, 1, 1);

    ID3D11UnorderedAccessView* nul[2] = { nullptr, nullptr };
    ctx->CSSetUnorderedAccessViews(0, 2, nul, nullptr);
    return true;
}

std::vector<std::uint32_t> Maelstrom::hamming_all(const lattice::Glyph& probe) const {
    std::vector<std::uint32_t> out;
    if (!impl_->run(probe, 1)) return out;
    impl_->ctx->CopyResource(impl_->outStaging.Get(), impl_->outBuf.Get());
    D3D11_MAPPED_SUBRESOURCE r{};
    if (FAILED(impl_->ctx->Map(impl_->outStaging.Get(), 0, D3D11_MAP_READ, 0, &r))) return out;
    out.resize(impl_->count);
    std::memcpy(out.data(), r.pData, static_cast<std::size_t>(impl_->count) * sizeof(std::uint32_t));
    impl_->ctx->Unmap(impl_->outStaging.Get(), 0);
    return out;
}

std::vector<Neighbour> Maelstrom::resonate(const lattice::Glyph& probe, std::size_t k) const {
    if (k < 1) k = 1;
    if (k > kMaxK) k = kMaxK;
    const UINT ku = static_cast<UINT>(k);
    if (!impl_->run(probe, ku)) return {};

    // Read back only the groups*k candidates the GPU pre-selected.
    const UINT used = impl_->groups * ku;
    D3D11_BOX box{};
    box.left  = 0;            box.right  = used * 2 * sizeof(UINT);
    box.top   = 0;            box.bottom = 1;
    box.front = 0;            box.back   = 1;
    impl_->ctx->CopySubresourceRegion(impl_->candStaging.Get(), 0, 0, 0, 0,
                                      impl_->candBuf.Get(), 0, &box);

    D3D11_MAPPED_SUBRESOURCE r{};
    if (FAILED(impl_->ctx->Map(impl_->candStaging.Get(), 0, D3D11_MAP_READ, 0, &r))) return {};
    struct Pair { std::uint32_t dist; std::uint32_t idx; };
    std::vector<Pair> cand(used);
    std::memcpy(cand.data(), r.pData, static_cast<std::size_t>(used) * sizeof(Pair));
    impl_->ctx->Unmap(impl_->candStaging.Get(), 0);

    // Drop sentinel lanes, then merge to the true global top-k.
    cand.erase(std::remove_if(cand.begin(), cand.end(),
                   [](const Pair& p) { return p.idx == 0xFFFFFFFFu || p.dist == 0xFFFFFFFFu; }),
               cand.end());
    const std::size_t kk = std::min(k, cand.size());
    std::partial_sort(cand.begin(), cand.begin() + kk, cand.end(),
        [](const Pair& a, const Pair& b) {
            if (a.dist != b.dist) return a.dist < b.dist;
            return a.idx < b.idx;
        });
    std::vector<Neighbour> out;
    out.reserve(kk);
    for (std::size_t i = 0; i < kk; ++i) out.push_back({ cand[i].idx, cand[i].dist });
    return out;
}

std::vector<std::vector<Neighbour>> Maelstrom::resonate_batch(
    const std::vector<lattice::Glyph>& probes, std::size_t k) const {
    if (!impl_->ready || impl_->count == 0 || probes.empty()) return {};
    if (k < 1) k = 1;
    if (k > kMaxK) k = kMaxK;
    const UINT ku = static_cast<UINT>(k);
    const UINT Q  = static_cast<UINT>(probes.size());
    // D3D11 caps a dispatch dimension at 65535 groups; one probe per Y group.
    if (Q > 65535) return {};

    auto& d = *impl_;
    const UINT groups = d.groups;

    // Pack the probes back-to-back and upload as an immutable structured SRV.
    std::vector<UINT> packed(static_cast<std::size_t>(Q) * kWords32);
    for (UINT p = 0; p < Q; ++p)
        std::memcpy(&packed[static_cast<std::size_t>(p) * kWords32],
                    probes[p].words().data(),
                    lattice::kGlyphWords * sizeof(lattice::Glyph::Word));

    ComPtr<ID3D11Buffer> qbuf;
    ComPtr<ID3D11ShaderResourceView> qsrv;
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth           = static_cast<UINT>(packed.size() * sizeof(UINT));
        bd.Usage               = D3D11_USAGE_IMMUTABLE;
        bd.BindFlags           = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(UINT);
        D3D11_SUBRESOURCE_DATA init{}; init.pSysMem = packed.data();
        if (FAILED(d.device->CreateBuffer(&bd, &init, &qbuf))) return {};
        D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_UNKNOWN;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        sv.BufferEx.NumElements = static_cast<UINT>(packed.size());
        if (FAILED(d.device->CreateShaderResourceView(qbuf.Get(), &sv, &qsrv))) return {};
    }

    // Output: Q * groups * k candidate pairs, plus a staging copy.
    const std::size_t candElems = static_cast<std::size_t>(Q) * groups * ku;
    ComPtr<ID3D11Buffer> cbuf, cstaging;
    ComPtr<ID3D11UnorderedAccessView> cuav;
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth           = static_cast<UINT>(candElems * 2 * sizeof(UINT));
        bd.Usage               = D3D11_USAGE_DEFAULT;
        bd.BindFlags           = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags           = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = 2 * sizeof(UINT);
        if (FAILED(d.device->CreateBuffer(&bd, nullptr, &cbuf))) return {};
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
        uv.Format = DXGI_FORMAT_UNKNOWN;
        uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uv.Buffer.NumElements = static_cast<UINT>(candElems);
        if (FAILED(d.device->CreateUnorderedAccessView(cbuf.Get(), &uv, &cuav))) return {};
        D3D11_BUFFER_DESC sd{};
        sd.ByteWidth      = static_cast<UINT>(candElems * 2 * sizeof(UINT));
        sd.Usage          = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(d.device->CreateBuffer(&sd, nullptr, &cstaging))) return {};
    }

    // Params reuse the {count, words, k, groups} layout.
    UINT params[4] = { d.count, kWords32, ku, groups };
    d.ctx->UpdateSubresource(d.paramBuf.Get(), 0, nullptr, params, 0, 0);

    ID3D11ShaderResourceView*  srvs[3] = { d.dbSRV.Get(), nullptr, qsrv.Get() };
    ID3D11UnorderedAccessView* uavs[3] = { nullptr, nullptr, cuav.Get() };
    ID3D11Buffer*              cb      = d.paramBuf.Get();
    d.ctx->CSSetShader(d.shaderBatch.Get(), nullptr, 0);
    d.ctx->CSSetShaderResources(0, 3, srvs);
    d.ctx->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
    d.ctx->CSSetConstantBuffers(0, 1, &cb);

    d.ctx->Dispatch(groups, Q, 1);

    ID3D11ShaderResourceView*  nsrv[3] = { nullptr, nullptr, nullptr };
    ID3D11UnorderedAccessView* nuav[3] = { nullptr, nullptr, nullptr };
    d.ctx->CSSetUnorderedAccessViews(0, 3, nuav, nullptr);
    d.ctx->CSSetShaderResources(0, 3, nsrv);

    d.ctx->CopyResource(cstaging.Get(), cbuf.Get());
    D3D11_MAPPED_SUBRESOURCE r{};
    if (FAILED(d.ctx->Map(cstaging.Get(), 0, D3D11_MAP_READ, 0, &r))) return {};
    struct Pair { std::uint32_t dist; std::uint32_t idx; };
    std::vector<Pair> all(candElems);
    std::memcpy(all.data(), r.pData, candElems * sizeof(Pair));
    d.ctx->Unmap(cstaging.Get(), 0);

    // Merge each probe's groups*k candidates to its global top-k.
    const std::size_t per = static_cast<std::size_t>(groups) * ku;
    std::vector<std::vector<Neighbour>> out(Q);
    for (UINT p = 0; p < Q; ++p) {
        std::vector<Pair> cand(all.begin() + static_cast<std::size_t>(p) * per,
                               all.begin() + static_cast<std::size_t>(p + 1) * per);
        cand.erase(std::remove_if(cand.begin(), cand.end(),
                       [](const Pair& q) { return q.idx == 0xFFFFFFFFu || q.dist == 0xFFFFFFFFu; }),
                   cand.end());
        const std::size_t kk = std::min(static_cast<std::size_t>(ku), cand.size());
        std::partial_sort(cand.begin(), cand.begin() + kk, cand.end(),
            [](const Pair& a, const Pair& b) {
                if (a.dist != b.dist) return a.dist < b.dist;
                return a.idx < b.idx;
            });
        out[p].reserve(kk);
        for (std::size_t i = 0; i < kk; ++i) out[p].push_back({ cand[i].idx, cand[i].dist });
    }
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
std::vector<std::vector<Neighbour>> Maelstrom::resonate_batch(
    const std::vector<lattice::Glyph>&, std::size_t) const { return {}; }
std::vector<std::uint32_t> Maelstrom::hamming_all(const lattice::Glyph&) const { return {}; }

} // namespace khora::maelstrom

#endif

// ---------------------------------------------------------------------------
// Resonator — labelled associative store with a CPU/GPU crossover. Lives
// outside the platform gate: it speaks only the public Maelstrom API, so the
// same code path works whether or not a GPU ever ignited.
// ---------------------------------------------------------------------------
namespace khora::maelstrom {

struct Resonator::RImpl {
    Maelstrom                    storm;
    std::vector<std::string>     labels;
    std::vector<lattice::Glyph>  glyphs;
    std::size_t                  crossover;
    bool                         gpu_active = false;
    explicit RImpl(std::size_t c) : crossover(c) {}
};

Resonator::Resonator(std::size_t gpu_crossover)
    : r_(std::make_unique<RImpl>(gpu_crossover)) {}
Resonator::~Resonator() = default;
Resonator::Resonator(Resonator&&) noexcept = default;
Resonator& Resonator::operator=(Resonator&&) noexcept = default;

void Resonator::build(std::vector<std::pair<std::string, lattice::Glyph>> entries) {
    r_->labels.clear();
    r_->glyphs.clear();
    r_->labels.reserve(entries.size());
    r_->glyphs.reserve(entries.size());
    for (auto& e : entries) {
        r_->labels.push_back(std::move(e.first));
        r_->glyphs.push_back(e.second);
    }
    r_->gpu_active = false;
    if (r_->glyphs.size() >= r_->crossover && r_->storm.ignite()) {
        r_->gpu_active = r_->storm.charge(r_->glyphs);
    }
}

void Resonator::build(const lattice::Lattice& lat) {
    std::vector<std::pair<std::string, lattice::Glyph>> entries;
    entries.reserve(lat.size());
    for (const auto& [label, g] : lat) entries.emplace_back(label, g);
    build(std::move(entries));
}

std::vector<lattice::LatticeMatch> Resonator::query(const lattice::Glyph& probe,
                                                    std::size_t k) const {
    using lattice::LatticeMatch;
    const auto to_sim = [](std::uint32_t h) {
        return 1.0 - 2.0 * static_cast<double>(h) / static_cast<double>(lattice::kGlyphBits);
    };

    // GPU path: resonate, then map indices back to labels.
    if (r_->gpu_active) {
        const auto neigh = r_->storm.resonate(probe, k);
        if (!neigh.empty() || r_->glyphs.empty()) {
            std::vector<LatticeMatch> out;
            out.reserve(neigh.size());
            for (const auto& nb : neigh)
                out.push_back({ r_->labels[nb.index], nb.hamming, to_sim(nb.hamming) });
            return out;
        }
        // fall through to CPU if the GPU returned nothing unexpectedly.
    }

    // CPU path: scan, partial-sort by (hamming, index) for determinism.
    const std::size_t n = r_->glyphs.size();
    std::vector<std::uint32_t> idx(n);
    std::vector<std::uint32_t> dist(n);
    for (std::size_t i = 0; i < n; ++i) {
        idx[i]  = static_cast<std::uint32_t>(i);
        dist[i] = static_cast<std::uint32_t>(probe.hamming(r_->glyphs[i]));
    }
    const std::size_t kk = std::min(k, n);
    std::partial_sort(idx.begin(), idx.begin() + kk, idx.end(),
        [&](std::uint32_t a, std::uint32_t b) {
            if (dist[a] != dist[b]) return dist[a] < dist[b];
            return a < b;
        });
    std::vector<LatticeMatch> out;
    out.reserve(kk);
    for (std::size_t i = 0; i < kk; ++i)
        out.push_back({ r_->labels[idx[i]], dist[idx[i]], to_sim(dist[idx[i]]) });
    return out;
}

bool        Resonator::on_gpu() const noexcept { return r_->gpu_active; }
std::size_t Resonator::size()   const noexcept { return r_->glyphs.size(); }
const DeviceInfo& Resonator::device() const noexcept { return r_->storm.device(); }

} // namespace khora::maelstrom
