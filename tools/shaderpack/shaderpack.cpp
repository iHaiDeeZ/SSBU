// The shader pack (scripts/build-shaderpack.sh): the game's DXBC shaders (windowsmash dump/shadercache - the
// ahead-of-time compiler's and the console's own) made on a PC through DXVK as the PS4 build has it
// (scripts/build-dxvk-host.sh) on a PC copy of the console's RADV (scripts/build-mesa-host.sh: the console's
// device and cache identity), so that the two caches the game reads on the console come out filled: RADV's
// (Mesa's single-file cache: the GPU code of each shader library) and DXVK's (each shader's IR). On the console
// a new shader then costs a cache read, not the second of compiling v00.15 measured (940 ms on average).
//
//   shaderpack <libvulkan_radeon.so> <list of DXBC paths> [first] [count] [threads]
//
// The device is made as the runtime makes it (runtime/gpu_d3d11.c: BGRA support, feature level 11.1 or 11.0) and
// each shader as it does (runtime/gpu_d3d11.c create_on: by the stage in its cache name, v<N>_<vs|ps|cs>_...).
// The environment says where the caches go and what DXVK and drm-shim are told (scripts/build-shaderpack.sh).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <dlfcn.h>

#include <d3d11.h>
#include <vulkan/vulkan.h>

namespace {

// ---- the app's Vulkan entry point, as the port's (port/ps4_vk_wsi.cpp) wraps RADV: the instance extensions DXVK's
// PS4 WSI asks for (VK_KHR_surface, VK_EXT_headless_surface) and VK_KHR_swapchain listed, and left out of what RADV
// is asked to create - it has no window system. Nothing is presented here: the presentation entry points only
// exist so that nothing calls through a null pointer.

PFN_vkGetInstanceProcAddr g_icd = nullptr;   // RADV's vk_icdGetInstanceProcAddr
VkInstance g_instance = VK_NULL_HANDLE;

template <typename T> T icd(VkInstance instance, const char* name) {
    return reinterpret_cast<T>(g_icd(instance, name));
}

VkResult append_extensions(std::vector<VkExtensionProperties>& list,
                           std::initializer_list<std::pair<const char*, uint32_t>> extra, uint32_t* count,
                           VkExtensionProperties* props) {
    for (const auto& [name, spec] : extra) {
        VkExtensionProperties p{};
        std::strncpy(p.extensionName, name, VK_MAX_EXTENSION_NAME_SIZE - 1);
        p.specVersion = spec;
        list.push_back(p);
    }
    if (!props) {
        *count = uint32_t(list.size());
        return VK_SUCCESS;
    }
    uint32_t n = std::min<uint32_t>(*count, uint32_t(list.size()));
    std::memcpy(props, list.data(), n * sizeof(VkExtensionProperties));
    *count = n;
    return n < list.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

std::vector<const char*> without(uint32_t count, const char* const* names, std::initializer_list<const char*> drop) {
    std::vector<const char*> out;
    for (uint32_t k = 0; k < count; k++)
        if (std::none_of(drop.begin(), drop.end(), [&](const char* d) { return std::strcmp(names[k], d) == 0; }))
            out.push_back(names[k]);
    return out;
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char* layer, uint32_t* count,
                                                                    VkExtensionProperties* props) {
    auto real = icd<PFN_vkEnumerateInstanceExtensionProperties>(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    uint32_t n = 0;
    real(layer, &n, nullptr);
    std::vector<VkExtensionProperties> list(n);
    real(layer, &n, list.data());
    list.resize(n);
    return append_extensions(list, {{VK_KHR_SURFACE_EXTENSION_NAME, 25}, {VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME, 1}},
                             count, props);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks* alloc,
                                              VkInstance* instance) {
    auto real = icd<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance");
    VkInstanceCreateInfo copy = *info;
    auto names = without(info->enabledExtensionCount, info->ppEnabledExtensionNames,
                         {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME});
    copy.enabledExtensionCount = uint32_t(names.size());
    copy.ppEnabledExtensionNames = names.data();
    VkResult r = real(&copy, alloc, instance);
    if (r == VK_SUCCESS) g_instance = *instance;
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physical, const char* layer,
                                                                  uint32_t* count, VkExtensionProperties* props) {
    auto real = icd<PFN_vkEnumerateDeviceExtensionProperties>(g_instance, "vkEnumerateDeviceExtensionProperties");
    uint32_t n = 0;
    real(physical, layer, &n, nullptr);
    std::vector<VkExtensionProperties> list(n);
    real(physical, layer, &n, list.data());
    list.resize(n);
    return append_extensions(list, {{VK_KHR_SWAPCHAIN_EXTENSION_NAME, 70}}, count, props);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
                                            const VkAllocationCallbacks* alloc, VkDevice* device) {
    auto real = icd<PFN_vkCreateDevice>(g_instance, "vkCreateDevice");
    VkDeviceCreateInfo copy = *info;
    auto names = without(info->enabledExtensionCount, info->ppEnabledExtensionNames, {VK_KHR_SWAPCHAIN_EXTENSION_NAME});
    copy.enabledExtensionCount = uint32_t(names.size());
    copy.ppEnabledExtensionNames = names.data();
    return real(physical, &copy, alloc, device);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateHeadlessSurfaceEXT(VkInstance, const VkHeadlessSurfaceCreateInfoEXT*,
                                                        const VkAllocationCallbacks*, VkSurfaceKHR* surface) {
    *surface = reinterpret_cast<VkSurfaceKHR>(uintptr_t(1));
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*) {}
VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t family, VkSurfaceKHR,
                                                                  VkBool32* supported) {
    *supported = family == 0 ? VK_TRUE : VK_FALSE;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL NoSurface() { return VK_ERROR_SURFACE_LOST_KHR; }

PFN_vkVoidFunction intercept(const char* name) {
#define SHADERPACK_ENTRY(fn) \
    if (std::strcmp(name, "vk" #fn) == 0) return reinterpret_cast<PFN_vkVoidFunction>(&fn)
    SHADERPACK_ENTRY(EnumerateInstanceExtensionProperties);
    SHADERPACK_ENTRY(CreateInstance);
    SHADERPACK_ENTRY(EnumerateDeviceExtensionProperties);
    SHADERPACK_ENTRY(CreateDevice);
    SHADERPACK_ENTRY(CreateHeadlessSurfaceEXT);
    SHADERPACK_ENTRY(DestroySurfaceKHR);
    SHADERPACK_ENTRY(GetPhysicalDeviceSurfaceSupportKHR);
#undef SHADERPACK_ENTRY
    static const char* const unpresented[] = {
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", "vkGetPhysicalDeviceSurfaceCapabilities2KHR",
        "vkGetPhysicalDeviceSurfaceFormatsKHR", "vkGetPhysicalDeviceSurfaceFormats2KHR",
        "vkGetPhysicalDeviceSurfacePresentModesKHR", "vkCreateSwapchainKHR", "vkGetSwapchainImagesKHR",
        "vkAcquireNextImageKHR", "vkAcquireNextImage2KHR", "vkQueuePresentKHR"};
    for (const char* u : unpresented)
        if (std::strcmp(name, u) == 0) return reinterpret_cast<PFN_vkVoidFunction>(&NoSurface);
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* name) {
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    if (auto fn = intercept(name)) return fn;
    static auto real = icd<PFN_vkGetDeviceProcAddr>(g_instance, "vkGetDeviceProcAddr");
    return real(device, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* name) {
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    if (auto fn = intercept(name)) return fn;
    return g_icd(instance, name);
}

// ---- the shaders -----------------------------------------------------------------------------------------------

enum class Stage { Vertex, Pixel, Compute, Unknown };

// runtime/gpu_d3d11.c's cache names: v<version>_<vs|ps|cs>_<hash>_<vs outputs>_<tables>.dxbc
Stage stage_of(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t u = name.find('_');
    if (u == std::string::npos || name.size() < u + 3) return Stage::Unknown;
    const std::string s = name.substr(u + 1, 2);
    return s == "vs" ? Stage::Vertex : s == "ps" ? Stage::Pixel : s == "cs" ? Stage::Compute : Stage::Unknown;
}

bool read_file(const std::string& path, std::vector<char>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    const bool ok = n > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// as runtime/gpu_d3d11.c create_on
HRESULT create(ID3D11Device* dev, Stage stage, const std::vector<char>& code) {
    IUnknown* obj = nullptr;
    HRESULT hr = E_INVALIDARG;
    if (stage == Stage::Vertex)
        hr = dev->CreateVertexShader(code.data(), code.size(), nullptr, reinterpret_cast<ID3D11VertexShader**>(&obj));
    else if (stage == Stage::Pixel)
        hr = dev->CreatePixelShader(code.data(), code.size(), nullptr, reinterpret_cast<ID3D11PixelShader**>(&obj));
    else if (stage == Stage::Compute)
        hr = dev->CreateComputeShader(code.data(), code.size(), nullptr, reinterpret_cast<ID3D11ComputeShader**>(&obj));
    if (obj) obj->Release();
    return hr;
}

}  // namespace

extern "C" PFN_vkGetInstanceProcAddr dxvk_ps4_vk_get_instance_proc_addr(void) { return &GetInstanceProcAddr; }

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <libvulkan_radeon.so> <list of DXBC paths> [first] [count] [threads]\n", argv[0]);
        return 2;
    }
    void* radv = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!radv || !(g_icd = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(radv, "vk_icdGetInstanceProcAddr")))) {
        std::fprintf(stderr, "shaderpack: cannot load RADV from %s: %s\n", argv[1], dlerror());
        return 1;
    }
    std::vector<std::string> files;
    if (FILE* list = std::fopen(argv[2], "r")) {
        char line[4096];
        while (std::fgets(line, sizeof line, list)) {
            line[std::strcspn(line, "\r\n")] = 0;
            if (line[0]) files.emplace_back(line);
        }
        std::fclose(list);
    }
    const size_t first = argc > 3 ? std::stoul(argv[3]) : 0;
    const size_t end = std::min(files.size(), argc > 4 ? first + std::stoul(argv[4]) : files.size());
    const unsigned threads = argc > 5 ? unsigned(std::stoul(argv[5])) : std::max(1u, std::thread::hardware_concurrency());
    if (first >= end) {
        std::fprintf(stderr, "shaderpack: nothing to do (%zu files, from %zu)\n", files.size(), first);
        return 0;
    }

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   2, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr)) {
        std::fprintf(stderr, "shaderpack: D3D11CreateDevice failed (%#x)\n", unsigned(hr));
        return 1;
    }

    std::atomic<size_t> next{first}, done{0}, failed{0};
    const auto t0 = std::chrono::steady_clock::now();
    auto work = [&] {
        std::vector<char> code;
        for (size_t i; (i = next++) < end;) {
            const Stage stage = stage_of(files[i]);
            if (stage == Stage::Unknown || !read_file(files[i], code) || FAILED(create(dev, stage, code))) {
                if (failed++ < 20) std::fprintf(stderr, "shaderpack: %s not made\n", files[i].c_str());
            }
            done++;
        }
    };
    std::vector<std::thread> pool;
    for (unsigned k = 0; k < threads; k++) pool.emplace_back(work);
    for (size_t last = 0; done.load() < end - first;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const size_t d = done.load();
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (d - last >= 200 || d == end - first) {
            std::fprintf(stderr, "shaderpack: %zu of %zu (%.0f a second)\n", first + d, files.size(), d / s);
            last = d;
        }
    }
    for (auto& t : pool) t.join();
    ctx->Release();
    dev->Release();   // (DXVK's cache written out, and Mesa's queued cache writes finished, as the device goes)
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::fprintf(stderr, "shaderpack: %zu shaders made in %.1f s, %zu failed\n", end - first, s, failed.load());
    return failed ? 3 : 0;
}
