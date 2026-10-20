// SSBU v00.00: Direct3D 11 through DXVK on the PS4's RADV, no game. Proves the graphics stack the port is built on
// (WindowSmash's renderer is D3D11): device + swapchain on VideoOut, DXBC shaders, a constant buffer, draws, presents.
// The screen cycles through colours behind a spinning triangle; /data/SSBU/dxvk-test.log says how far it got, DXVK's
// own log is /data/SSBU/eboot_d3d11.log and /data/SSBU/eboot_dxgi.log.
#include <d3d11.h>
#include <dxgi.h>

#include <orbis/libkernel.h>

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sys/stat.h>

#include "../../port/ps4_vk_wsi.h"
#include "shaders.h"  // make_shaders.py: DXBC made by Windows' d3dcompiler_47

extern "C" void ps4_boot_trace(const char* stage);  // port/ps4_crashlog.cpp (installed by a constructor)

namespace {

FILE* g_log = nullptr;
std::mutex g_log_lock;

void logf(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    std::lock_guard lk(g_log_lock);
    if (g_log) {
        fprintf(g_log, "%s\n", line);
        fflush(g_log);
    }
    printf("%s\n", line);
}

#define CHECK(what, expr)                                                    \
    do {                                                                     \
        HRESULT hr_ = (expr);                                                \
        if (FAILED(hr_)) {                                                   \
            logf("FAILED: %s = 0x%08x", what, unsigned(hr_));                \
            for (;;) sceKernelSleep(1);                                      \
        }                                                                    \
        logf("ok: %s", what);                                                \
    } while (0)

struct Constants {
    float angle, aspect, pad[2];
    float tint[4];
};

}  // namespace

void ssbu::ps4::wsi_log(const char* line) { logf("%s", line); }

// port/ps4_crashlog.cpp's hook for the guest's state at a crash (none here; create-fself refuses an undefined one)
extern "C" void ps4_crash_guest_info() {}

int main() {
    mkdir("/data/SSBU", 0777);
    g_log = fopen("/data/SSBU/dxvk-test.log", "w");
    logf("SSBU 00.00 starting: DXVK test");
    // DXVK: its log next to ours, the PS4 WSI, no state cache on the slow drive yet
    setenv("DXVK_LOG_PATH", "/data/SSBU", 1);
    setenv("DXVK_LOG_LEVEL", "info", 1);
    setenv("DXVK_WSI_DRIVER", "PS4", 1);
    setenv("DXVK_CONFIG_FILE", "/data/SSBU/dxvk.conf", 0);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width = ssbu::ps4::kScreenWidth;
    sd.BufferDesc.Height = ssbu::ps4::kScreenHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = {60, 1};
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = reinterpret_cast<HWND>(uintptr_t(1));  // the PS4 WSI's one window (the screen)
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapchain = nullptr;
    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0, got{};
    CHECK("D3D11CreateDeviceAndSwapChain",
          D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &want, 1, D3D11_SDK_VERSION, &sd,
                                        &swapchain, &device, &got, &context));
    logf("feature level %x", unsigned(got));

    ID3D11Texture2D* back = nullptr;
    CHECK("GetBuffer", swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)));
    ID3D11RenderTargetView* rtv = nullptr;
    CHECK("CreateRenderTargetView", device->CreateRenderTargetView(back, nullptr, &rtv));
    back->Release();

    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    CHECK("CreateVertexShader", device->CreateVertexShader(kVS, sizeof kVS, nullptr, &vs));
    CHECK("CreatePixelShader", device->CreatePixelShader(kPS, sizeof kPS, nullptr, &ps));

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(Constants);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* cb = nullptr;
    CHECK("CreateBuffer (constants)", device->CreateBuffer(&cbd, nullptr, &cb));

    D3D11_VIEWPORT vp{0, 0, float(ssbu::ps4::kScreenWidth), float(ssbu::ps4::kScreenHeight), 0, 1};
    auto start = std::chrono::steady_clock::now();
    auto window = start;
    uint64_t frames = 0, window_frames = 0;
    for (;;) {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        float clear[4] = {0.5f + 0.5f * float(std::sin(t)), 0.5f + 0.5f * float(std::sin(t + 2.1)),
                          0.5f + 0.5f * float(std::sin(t + 4.2)), 1.0f};
        context->ClearRenderTargetView(rtv, clear);

        D3D11_MAPPED_SUBRESOURCE m{};
        if (SUCCEEDED(context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            Constants c{float(t), float(ssbu::ps4::kScreenHeight) / float(ssbu::ps4::kScreenWidth), {0, 0},
                        {1.0f - clear[0], 1.0f - clear[1], 1.0f - clear[2], 1.0f}};
            std::memcpy(m.pData, &c, sizeof c);
            context->Unmap(cb, 0);
        }
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->RSSetViewports(1, &vp);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &cb);
        context->PSSetShader(ps, nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &cb);
        context->Draw(3, 0);  // the vertex shader makes the triangle from SV_VertexID

        HRESULT hr = swapchain->Present(1, 0);
        frames++;
        window_frames++;
        if (FAILED(hr)) {
            logf("Present failed: 0x%08x after %llu frames", unsigned(hr), (unsigned long long)frames);
            for (;;) sceKernelSleep(1);
        }
        if (frames <= 3) logf("present %llu ok", (unsigned long long)frames);
        auto now = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(now - window).count();
        if (secs >= 5.0) {
            logf("%.1f fps (%llu frames, %llu flips)", double(window_frames) / secs, (unsigned long long)frames,
                 (unsigned long long)ssbu::ps4::vk_flips());
            window = now;
            window_frames = 0;
        }
    }
}
