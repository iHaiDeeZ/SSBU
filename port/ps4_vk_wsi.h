// PS4: Vulkan window system integration on VideoOut for DXVK (ps4_vk_wsi.cpp, from SMW's).
#pragma once

#include <cstdint>

namespace ssbu::ps4 {

// VideoOut's 1080p output (the system scales it for a 720p TV).
constexpr uint32_t kScreenWidth = 1920;
constexpr uint32_t kScreenHeight = 1080;

// vkGetInstanceProcAddr of the statically linked RADV with the PS4 surface and swapchain (VK_EXT_headless_surface +
// VK_KHR_swapchain on VideoOut) added. A PFN_vkGetInstanceProcAddr.
void* vk_get_instance_proc_addr();

// Flips submitted so far (the first one hides the system's splash screen).
uint64_t vk_flips();

// Where the WSI's lines go (defined by the app).
void wsi_log(const char* line);

}  // namespace ssbu::ps4
