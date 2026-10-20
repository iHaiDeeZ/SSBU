// PS4: Vulkan window system integration on VideoOut, for DXVK (SMW's runtime/src/ps4/ps4_vk_wsi.cpp, from
// Resonance's ps4_wsi.cpp and Crema's PS4WSI.cpp, without SMW's boot screen, quick menu and frame counter).
//
// RADV (Crema's GNM build) is linked into the executable and has no WSI. This file adds VK_EXT_headless_surface and
// VK_KHR_swapchain on top of it: the swapchain images are linear R8G8B8A8 images in host-visible memory (GPU address
// = CPU address on the PS4), registered as VideoOut buffers, and presenting queues a VideoOut flip behind the GPU
// work submitted so far (ac_ps4_submit_flip in the Mesa winsys). DXVK reaches all of it through
// dxvk_ps4_vk_get_instance_proc_addr() (its src/vulkan/vulkan_loader.cpp on PS4).
//
// Semaphores: there is one in-order queue, and on the PS4 a submit that waits for or signals a binary semaphore with
// no other work loses the device (Crema, Resonance). The acquire semaphore is never signalled here (the image is off
// screen when vkAcquireNextImageKHR returns), so every submit drops its BINARY semaphores; timeline semaphores, which
// DXVK tracks its GPU work with, are kept (the winsys emulates timeline sync objects).
#include "ps4_vk_wsi.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <orbis/SystemService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* name);
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_common_GetDeviceProcAddr(VkDevice device, const char* name);
// port/mesa/ac_ps4_drm.c: queues a VideoOut flip behind the submitted GPU work (GPU-side flip)
extern "C" int ac_ps4_submit_flip(int video, unsigned buffer, unsigned mode, int64_t arg);

namespace ssbu::ps4 {

namespace {

constexpr uint32_t kMaxImages = 4;
constexpr VkDeviceSize kFramebufferAlign = 2 << 20;  // VideoOut buffer alignment

struct Surface {
    uint32_t magic = 0x50533453;
};

struct Swapchain {
    VkDevice device;
    uint32_t image_count;
    VkImage images[kMaxImages]{};
    VkDeviceMemory memory[kMaxImages]{};
    int buffer_set = -1;
    int first_buffer = 0;
    uint32_t next_image = 0;
    uint32_t flip_mode = ORBIS_VIDEO_OUT_FLIP_VSYNC;
    uint32_t pitch = 0;
};

VkInstance g_instance = VK_NULL_HANDLE;
VkPhysicalDevice g_physical = VK_NULL_HANDLE;
VkDevice g_device = VK_NULL_HANDLE;
int g_video = -1;
OrbisKernelEqueue g_flip_queue = 0;
std::atomic<uint64_t> g_flips{0};
int g_next_first_buffer = 0;
std::mutex g_lock;
// timeline semaphores (kept in submits; every other semaphore is dropped)
std::shared_mutex g_timeline_lock;
std::unordered_set<VkSemaphore> g_timelines;

void log(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    wsi_log(line);
}

template <typename T>
T device_fn(VkDevice device, const char* name) {
    return reinterpret_cast<T>(vk_common_GetDeviceProcAddr(device, name));
}

template <typename T>
T instance_fn(const char* name) {
    return reinterpret_cast<T>(vk_icdGetInstanceProcAddr(g_instance, name));
}

bool open_video_out() {
    if (g_video >= 0) return true;
    g_video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, nullptr);
    if (g_video < 0) {
        log("ps4/wsi: sceVideoOutOpen failed: %x", unsigned(g_video));
        return false;
    }
    if (sceKernelCreateEqueue(&g_flip_queue, "ssbu flip") < 0 ||
        sceVideoOutAddFlipEvent(g_flip_queue, g_video, nullptr) < 0) {
        log("ps4/wsi: no flip event queue");
        g_flip_queue = 0;
    }
    return true;
}

// Appends extensions to an enumeration result (count/properties two-call idiom).
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

// ---- instance level -------------------------------------------------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL EnumerateInstanceExtensionProperties(const char* layer, uint32_t* count,
                                                                    VkExtensionProperties* props) {
    auto real = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
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
    auto real = reinterpret_cast<PFN_vkCreateInstance>(vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    VkInstanceCreateInfo copy = *info;
    auto names = without(info->enabledExtensionCount, info->ppEnabledExtensionNames,
                         {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME});
    copy.enabledExtensionCount = uint32_t(names.size());
    copy.ppEnabledExtensionNames = names.data();
    VkResult r = real(&copy, alloc, instance);
    if (r == VK_SUCCESS) g_instance = *instance;
    log("ps4/wsi: vkCreateInstance = %d", int(r));
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(VkPhysicalDevice physical, const char* layer,
                                                                  uint32_t* count, VkExtensionProperties* props) {
    auto real = instance_fn<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
    uint32_t n = 0;
    real(physical, layer, &n, nullptr);
    std::vector<VkExtensionProperties> list(n);
    real(physical, layer, &n, list.data());
    list.resize(n);
    return append_extensions(list, {{VK_KHR_SWAPCHAIN_EXTENSION_NAME, 70}}, count, props);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* info,
                                            const VkAllocationCallbacks* alloc, VkDevice* device) {
    auto real = instance_fn<PFN_vkCreateDevice>("vkCreateDevice");
    VkDeviceCreateInfo copy = *info;
    auto names = without(info->enabledExtensionCount, info->ppEnabledExtensionNames, {VK_KHR_SWAPCHAIN_EXTENSION_NAME});
    copy.enabledExtensionCount = uint32_t(names.size());
    copy.ppEnabledExtensionNames = names.data();
    g_physical = physical;
    VkResult r = real(physical, &copy, alloc, device);
    if (r == VK_SUCCESS) g_device = *device;
    log("ps4/wsi: vkCreateDevice with %u extensions = %d", copy.enabledExtensionCount, int(r));
    for (uint32_t k = 0; k < copy.enabledExtensionCount; k++) log("ps4/wsi:   %s", copy.ppEnabledExtensionNames[k]);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateHeadlessSurfaceEXT(VkInstance, const VkHeadlessSurfaceCreateInfoEXT*,
                                                        const VkAllocationCallbacks*, VkSurfaceKHR* surface) {
    *surface = reinterpret_cast<VkSurfaceKHR>(new Surface());
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroySurfaceKHR(VkInstance, VkSurfaceKHR surface, const VkAllocationCallbacks*) {
    delete reinterpret_cast<Surface*>(surface);
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t family, VkSurfaceKHR,
                                                                  VkBool32* supported) {
    *supported = family == 0 ? VK_TRUE : VK_FALSE;  // the graphics queue family
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR,
                                                                       VkSurfaceCapabilitiesKHR* caps) {
    *caps = {};
    caps->minImageCount = 2;
    caps->maxImageCount = 3;  // as DolphinPS4: render into one while one is shown and one is queued
    caps->currentExtent = {kScreenWidth, kScreenHeight};
    caps->minImageExtent = caps->currentExtent;
    caps->maxImageExtent = caps->currentExtent;
    caps->maxImageArrayLayers = 1;
    caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    caps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    caps->supportedUsageFlags =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physical,
                                                                        const VkPhysicalDeviceSurfaceInfo2KHR* info,
                                                                        VkSurfaceCapabilities2KHR* caps) {
    return GetPhysicalDeviceSurfaceCapabilitiesKHR(physical, info->surface, &caps->surfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t* count,
                                                                  VkSurfaceFormatKHR* formats) {
    // VideoOut A8B8G8R8 = R8G8B8A8 in memory (verified by DolphinPS4)
    if (!formats) {
        *count = 1;
        return VK_SUCCESS;
    }
    if (*count < 1) return VK_INCOMPLETE;
    formats[0] = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    *count = 1;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t* count,
                                                                       VkPresentModeKHR* modes) {
    const VkPresentModeKHR supported[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
                                          VK_PRESENT_MODE_IMMEDIATE_KHR};
    if (!modes) {
        *count = uint32_t(std::size(supported));
        return VK_SUCCESS;
    }
    uint32_t n = std::min<uint32_t>(*count, uint32_t(std::size(supported)));
    std::memcpy(modes, supported, n * sizeof(VkPresentModeKHR));
    *count = n;
    return n < std::size(supported) ? VK_INCOMPLETE : VK_SUCCESS;
}

// ---- device level ---------------------------------------------------------------------------------------------

uint32_t host_visible_type(uint32_t bits) {
    VkPhysicalDeviceMemoryProperties props;
    instance_fn<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(g_physical, &props);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t k = 0; k < props.memoryTypeCount; k++)
        if ((bits & (1u << k)) && (props.memoryTypes[k].propertyFlags & want) == want) return k;
    return UINT32_MAX;
}

void free_swapchain(Swapchain* sc) {
    if (sc->buffer_set >= 0) sceVideoOutUnregisterBuffers(g_video, sc->buffer_set);
    auto destroy_image = device_fn<PFN_vkDestroyImage>(sc->device, "vkDestroyImage");
    auto free_memory = device_fn<PFN_vkFreeMemory>(sc->device, "vkFreeMemory");
    for (uint32_t k = 0; k < sc->image_count; k++) {
        if (sc->images[k]) destroy_image(sc->device, sc->images[k], nullptr);
        if (sc->memory[k]) free_memory(sc->device, sc->memory[k], nullptr);
    }
    delete sc;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* info,
                                                  const VkAllocationCallbacks*, VkSwapchainKHR* swapchain) {
    std::lock_guard lk(g_lock);
    if (!open_video_out()) return VK_ERROR_INITIALIZATION_FAILED;
    auto sc = new Swapchain();
    sc->device = device;
    sc->image_count = std::clamp<uint32_t>(info->minImageCount, 2, kMaxImages);
    // IMMEDIATE flips at the next scanline (tearing); everything else at the next vblank.
    sc->flip_mode =
        info->presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? ORBIS_VIDEO_OUT_FLIP_HSYNC : ORBIS_VIDEO_OUT_FLIP_VSYNC;
    auto create_image = device_fn<PFN_vkCreateImage>(device, "vkCreateImage");
    auto requirements = device_fn<PFN_vkGetImageMemoryRequirements>(device, "vkGetImageMemoryRequirements");
    auto allocate = device_fn<PFN_vkAllocateMemory>(device, "vkAllocateMemory");
    auto map = device_fn<PFN_vkMapMemory>(device, "vkMapMemory");
    auto bind = device_fn<PFN_vkBindImageMemory>(device, "vkBindImageMemory");
    auto layout_of = device_fn<PFN_vkGetImageSubresourceLayout>(device, "vkGetImageSubresourceLayout");
    void* addresses[kMaxImages]{};
    uint32_t pitch = kScreenWidth;
    for (uint32_t k = 0; k < sc->image_count; k++) {
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = {kScreenWidth, kScreenHeight, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_LINEAR;
        ici.usage = info->imageUsage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult r = create_image(device, &ici, nullptr, &sc->images[k]);
        if (r != VK_SUCCESS) {
            log("ps4/wsi: vkCreateImage failed: %d", int(r));
            free_swapchain(sc);
            return r;
        }
        VkMemoryRequirements req;
        requirements(device, sc->images[k], &req);
        // VideoOut wants 2 MiB aligned buffers: over-allocate, bind at an aligned offset
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size + kFramebufferAlign;
        mai.memoryTypeIndex = host_visible_type(req.memoryTypeBits);
        uint8_t* base = nullptr;
        r = allocate(device, &mai, nullptr, &sc->memory[k]);
        if (r == VK_SUCCESS) r = map(device, sc->memory[k], 0, VK_WHOLE_SIZE, 0, reinterpret_cast<void**>(&base));
        if (r != VK_SUCCESS) {
            log("ps4/wsi: swapchain memory failed: %d", int(r));
            free_swapchain(sc);
            return r;
        }
        uintptr_t at = reinterpret_cast<uintptr_t>(base);
        VkDeviceSize offset = ((at + kFramebufferAlign - 1) & ~(kFramebufferAlign - 1)) - at;
        offset = (offset + req.alignment - 1) & ~(req.alignment - 1);
        bind(device, sc->images[k], sc->memory[k], offset);
        VkImageSubresource sub{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
        VkSubresourceLayout layout;
        layout_of(device, sc->images[k], &sub, &layout);
        addresses[k] = base + offset + layout.offset;
        pitch = uint32_t(layout.rowPitch / 4);
        sc->pitch = pitch;
    }
    OrbisVideoOutBufferAttribute attribute;
    sceVideoOutSetBufferAttribute(&attribute, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, 0, kScreenWidth, kScreenHeight, pitch);
    // Two buffer index ranges in turn, so a new swapchain can register while the old one is still on screen.
    sc->first_buffer = g_next_first_buffer;
    g_next_first_buffer = g_next_first_buffer == 0 ? 8 : 0;
    sc->buffer_set =
        sceVideoOutRegisterBuffers(g_video, sc->first_buffer, addresses, int(sc->image_count), &attribute);
    if (sc->buffer_set < 0) {
        log("ps4/wsi: sceVideoOutRegisterBuffers failed: %x", unsigned(sc->buffer_set));
        free_swapchain(sc);
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    log("ps4/wsi: swapchain of %u images %ux%u pitch %u, buffers %d+, %s flips", sc->image_count, kScreenWidth,
        kScreenHeight, pitch, sc->first_buffer, sc->flip_mode == ORBIS_VIDEO_OUT_FLIP_VSYNC ? "vsync" : "hsync");
    *swapchain = reinterpret_cast<VkSwapchainKHR>(sc);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                               const VkAllocationCallbacks*) {
    if (!swapchain) return;
    std::lock_guard lk(g_lock);
    device_fn<PFN_vkDeviceWaitIdle>(device, "vkDeviceWaitIdle")(device);
    free_swapchain(reinterpret_cast<Swapchain*>(swapchain));
}

VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainImagesKHR(VkDevice, VkSwapchainKHR swapchain, uint32_t* count,
                                                     VkImage* images) {
    auto sc = reinterpret_cast<Swapchain*>(swapchain);
    if (!images) {
        *count = sc->image_count;
        return VK_SUCCESS;
    }
    uint32_t n = std::min(*count, sc->image_count);
    std::memcpy(images, sc->images, n * sizeof(VkImage));
    *count = n;
    return n < sc->image_count ? VK_INCOMPLETE : VK_SUCCESS;
}

// Present statistics, logged every 10 s.
struct {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    uint64_t flips = 0, wait_ns = 0;
    uint32_t timeouts = 0;
    int64_t last_arg = 0;
} g_stats;

// The image about to be rendered into must be off screen: with N images in rotation it was shown by flip n-N+1 and
// replaced on screen by flip n-N+2 (n = flips submitted).
void wait_off_screen(uint32_t images) {
    uint64_t submitted = g_flips.load();
    if (submitted < images - 1) return;
    const int64_t needed = int64_t(submitted - (images - 2));
    auto t0 = std::chrono::steady_clock::now();
    bool done = false;
    for (int tries = 0; tries < 40 && !done; tries++) {
        OrbisVideoOutFlipStatus status{};
        if (sceVideoOutGetFlipStatus(g_video, &status) < 0) break;
        g_stats.last_arg = status.flipArg;
        if (status.flipArg >= needed) {
            done = true;
            break;
        }
        if (!g_flip_queue) {
            sceKernelUsleep(1000);
            continue;
        }
        OrbisKernelEvent ev;
        int got = 0;
        OrbisKernelUseconds timeout = 5000;
        sceKernelWaitEqueue(g_flip_queue, &ev, 1, &got, &timeout);
    }
    if (!done) g_stats.timeouts++;
    g_stats.wait_ns +=
        uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
}

void count_present() {
    g_stats.flips++;
    auto now = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(now - g_stats.start).count();
    if (secs < 10.0) return;
    log("ps4/wsi: %.1f flips/s, acquire waits %.0f ms/s, %u timeouts, flipArg %lld of %llu submitted",
        double(g_stats.flips) / secs, double(g_stats.wait_ns) / 1e6 / secs, g_stats.timeouts,
        (long long)g_stats.last_arg, (unsigned long long)g_flips.load());
    g_stats.start = now;
    g_stats.flips = g_stats.wait_ns = 0;
    g_stats.timeouts = 0;
}

VkQueue graphics_queue(VkDevice device) {
    VkQueue q = VK_NULL_HANDLE;
    device_fn<PFN_vkGetDeviceQueue>(device, "vkGetDeviceQueue")(device, 0, 0, &q);
    return q;
}

VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t, VkSemaphore,
                                                   VkFence fence, uint32_t* index) {
    auto sc = reinterpret_cast<Swapchain*>(swapchain);
    wait_off_screen(sc->image_count);
    *index = sc->next_image;
    sc->next_image = (sc->next_image + 1) % sc->image_count;
    // The image is free now. The semaphore is never signalled (see the top of the file); a fence, if asked for, is
    // signalled by an empty submit.
    if (fence) {
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        VkResult r = device_fn<PFN_vkQueueSubmit>(device, "vkQueueSubmit")(graphics_queue(device), 1, &si, fence);
        if (r != VK_SUCCESS) log("ps4/wsi: acquire fence submit failed: %d", int(r));
        return r;
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* info,
                                                    uint32_t* index) {
    return AcquireNextImageKHR(device, info->swapchain, info->timeout, info->semaphore, info->fence, index);
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    VkResult result = VK_SUCCESS;
    for (uint32_t k = 0; k < info->swapchainCount; k++) {
        auto sc = reinterpret_cast<Swapchain*>(info->pSwapchains[k]);
        const uint32_t buffer = uint32_t(sc->first_buffer) + info->pImageIndices[k];
        const int64_t arg = int64_t(g_flips.load() + 1);
        // the flip goes to the GPU behind the frame's commands: no CPU wait for the GPU or the vblank
        int r = ac_ps4_submit_flip(g_video, buffer, sc->flip_mode, arg);
        const bool gpu_flip = r == 0;
        if (!gpu_flip) {  // fallback: wait for the GPU on the CPU, then flip
            device_fn<PFN_vkQueueWaitIdle>(sc->device, "vkQueueWaitIdle")(queue);
            r = sceVideoOutSubmitFlip(g_video, int(buffer), sc->flip_mode, arg);
        }
        if (r < 0) {
            log("ps4/wsi: flip failed: %x", unsigned(r));
            result = VK_ERROR_SURFACE_LOST_KHR;
        } else {
            uint64_t n = ++g_flips;
            count_present();
            // The system keeps its boot splash (the app's picture) over everything until the app says it has a
            // picture of its own.
            if (n == 1) sceSystemServiceHideSplashScreen();
            if (n <= 3)
                log("ps4/wsi: flip %llu of buffer %u (%s)", (unsigned long long)n, buffer,
                    gpu_flip ? "GPU-side" : "CPU wait");
        }
        if (info->pResults) info->pResults[k] = r < 0 ? VK_ERROR_SURFACE_LOST_KHR : VK_SUCCESS;
    }
    return result;
}

// ---- semaphores: binary ones are dropped from submits, timeline ones kept -------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL CreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* info,
                                               const VkAllocationCallbacks* alloc, VkSemaphore* semaphore) {
    VkResult r = device_fn<PFN_vkCreateSemaphore>(device, "vkCreateSemaphore")(device, info, alloc, semaphore);
    if (r != VK_SUCCESS) return r;
    for (auto p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO &&
            reinterpret_cast<const VkSemaphoreTypeCreateInfo*>(p)->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE) {
            std::unique_lock lk(g_timeline_lock);
            g_timelines.insert(*semaphore);
        }
    return r;
}

VKAPI_ATTR void VKAPI_CALL DestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                            const VkAllocationCallbacks* alloc) {
    {
        std::unique_lock lk(g_timeline_lock);
        g_timelines.erase(semaphore);
    }
    device_fn<PFN_vkDestroySemaphore>(device, "vkDestroySemaphore")(device, semaphore, alloc);
}

bool is_timeline(VkSemaphore s) {
    std::shared_lock lk(g_timeline_lock);
    return g_timelines.count(s) != 0;
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit(VkQueue queue, uint32_t count, const VkSubmitInfo* submits, VkFence fence) {
    static PFN_vkQueueSubmit real = nullptr;
    if (!real) real = device_fn<PFN_vkQueueSubmit>(g_device, "vkQueueSubmit");
    std::vector<VkSubmitInfo> copy(submits, submits + count);
    std::vector<std::vector<VkSemaphore>> sems(count * 2);
    std::vector<std::vector<VkPipelineStageFlags>> stages(count);
    std::vector<std::vector<uint64_t>> values(count * 2);
    std::vector<VkTimelineSemaphoreSubmitInfo> tl(count);
    for (uint32_t k = 0; k < count; k++) {
        const VkTimelineSemaphoreSubmitInfo* t = nullptr;
        for (auto p = static_cast<const VkBaseInStructure*>(submits[k].pNext); p; p = p->pNext)
            if (p->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO)
                t = reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(p);
        for (uint32_t w = 0; w < submits[k].waitSemaphoreCount; w++)
            if (is_timeline(submits[k].pWaitSemaphores[w])) {
                sems[2 * k].push_back(submits[k].pWaitSemaphores[w]);
                stages[k].push_back(submits[k].pWaitDstStageMask[w]);
                values[2 * k].push_back(t && w < t->waitSemaphoreValueCount ? t->pWaitSemaphoreValues[w] : 0);
            }
        for (uint32_t s = 0; s < submits[k].signalSemaphoreCount; s++)
            if (is_timeline(submits[k].pSignalSemaphores[s])) {
                sems[2 * k + 1].push_back(submits[k].pSignalSemaphores[s]);
                values[2 * k + 1].push_back(t && s < t->signalSemaphoreValueCount ? t->pSignalSemaphoreValues[s] : 0);
            }
        copy[k].waitSemaphoreCount = uint32_t(sems[2 * k].size());
        copy[k].pWaitSemaphores = sems[2 * k].data();
        copy[k].pWaitDstStageMask = stages[k].data();
        copy[k].signalSemaphoreCount = uint32_t(sems[2 * k + 1].size());
        copy[k].pSignalSemaphores = sems[2 * k + 1].data();
        if (t) {  // (the struct is const in the chain: rebuilt with the kept values, chained in front)
            tl[k] = *t;
            tl[k].waitSemaphoreValueCount = uint32_t(values[2 * k].size());
            tl[k].pWaitSemaphoreValues = values[2 * k].data();
            tl[k].signalSemaphoreValueCount = uint32_t(values[2 * k + 1].size());
            tl[k].pSignalSemaphoreValues = values[2 * k + 1].data();
            tl[k].pNext = nullptr;
            copy[k].pNext = &tl[k];
        }
    }
    return real(queue, count, copy.data(), fence);
}

VKAPI_ATTR VkResult VKAPI_CALL QueueSubmit2(VkQueue queue, uint32_t count, const VkSubmitInfo2* submits,
                                            VkFence fence) {
    static PFN_vkQueueSubmit2 real = nullptr;
    if (!real) real = device_fn<PFN_vkQueueSubmit2>(g_device, "vkQueueSubmit2");
    std::vector<VkSubmitInfo2> copy(submits, submits + count);
    std::vector<std::vector<VkSemaphoreSubmitInfo>> sems(count * 2);
    for (uint32_t k = 0; k < count; k++) {
        for (uint32_t w = 0; w < submits[k].waitSemaphoreInfoCount; w++)
            if (is_timeline(submits[k].pWaitSemaphoreInfos[w].semaphore))
                sems[2 * k].push_back(submits[k].pWaitSemaphoreInfos[w]);
        for (uint32_t s = 0; s < submits[k].signalSemaphoreInfoCount; s++)
            if (is_timeline(submits[k].pSignalSemaphoreInfos[s].semaphore))
                sems[2 * k + 1].push_back(submits[k].pSignalSemaphoreInfos[s]);
        copy[k].waitSemaphoreInfoCount = uint32_t(sems[2 * k].size());
        copy[k].pWaitSemaphoreInfos = sems[2 * k].data();
        copy[k].signalSemaphoreInfoCount = uint32_t(sems[2 * k + 1].size());
        copy[k].pSignalSemaphoreInfos = sems[2 * k + 1].data();
    }
    return real(queue, count, copy.data(), fence);
}

PFN_vkVoidFunction intercept(const char* name) {
#define SSBU_WSI_ENTRY(fn) \
    if (std::strcmp(name, "vk" #fn) == 0) return reinterpret_cast<PFN_vkVoidFunction>(&fn)
#define SSBU_WSI_ALIAS(alias, fn) \
    if (std::strcmp(name, "vk" #alias) == 0) return reinterpret_cast<PFN_vkVoidFunction>(&fn)
    SSBU_WSI_ENTRY(EnumerateInstanceExtensionProperties);
    SSBU_WSI_ENTRY(CreateInstance);
    SSBU_WSI_ENTRY(EnumerateDeviceExtensionProperties);
    SSBU_WSI_ENTRY(CreateDevice);
    SSBU_WSI_ENTRY(CreateHeadlessSurfaceEXT);
    SSBU_WSI_ENTRY(DestroySurfaceKHR);
    SSBU_WSI_ENTRY(GetPhysicalDeviceSurfaceSupportKHR);
    SSBU_WSI_ENTRY(GetPhysicalDeviceSurfaceCapabilitiesKHR);
    SSBU_WSI_ENTRY(GetPhysicalDeviceSurfaceCapabilities2KHR);
    SSBU_WSI_ENTRY(GetPhysicalDeviceSurfaceFormatsKHR);
    SSBU_WSI_ENTRY(GetPhysicalDeviceSurfacePresentModesKHR);
    SSBU_WSI_ENTRY(CreateSwapchainKHR);
    SSBU_WSI_ENTRY(DestroySwapchainKHR);
    SSBU_WSI_ENTRY(GetSwapchainImagesKHR);
    SSBU_WSI_ENTRY(AcquireNextImageKHR);
    SSBU_WSI_ENTRY(AcquireNextImage2KHR);
    SSBU_WSI_ENTRY(QueuePresentKHR);
    SSBU_WSI_ENTRY(CreateSemaphore);
    SSBU_WSI_ENTRY(DestroySemaphore);
    SSBU_WSI_ENTRY(QueueSubmit);
    SSBU_WSI_ENTRY(QueueSubmit2);
    SSBU_WSI_ALIAS(QueueSubmit2KHR, QueueSubmit2);
#undef SSBU_WSI_ALIAS
#undef SSBU_WSI_ENTRY
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice device, const char* name) {
    // A loader that looks vkGetDeviceProcAddr up again through itself must get this one: RADV's would resolve the
    // swapchain functions, and it has none (all null).
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    if (auto fn = intercept(name)) return fn;
    return vk_common_GetDeviceProcAddr(device, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance instance, const char* name) {
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr);
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    if (auto fn = intercept(name)) return fn;
    return vk_icdGetInstanceProcAddr(instance, name);
}

}  // namespace

void* vk_get_instance_proc_addr() { return reinterpret_cast<void*>(&GetInstanceProcAddr); }

uint64_t vk_flips() { return g_flips.load(); }

}  // namespace ssbu::ps4

// DXVK's src/vulkan/vulkan_loader.cpp (patches/dxvk-ps4.patch)
extern "C" PFN_vkGetInstanceProcAddr dxvk_ps4_vk_get_instance_proc_addr(void) {
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(ssbu::ps4::vk_get_instance_proc_addr());
}
