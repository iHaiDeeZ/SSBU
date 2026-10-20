// The SSBU port's hooks into WindowSmash's PS4 runtime (runtime/ps4 in patches/windowsmash-ps4.patch) and into the
// PS4 layer (port/): its start (the screen opened before any graphics, as SMW's boot screen does; DXVK's settings),
// where the Vulkan WSI's lines go, and the crash log's guest part.
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include "../port/ps4_vk_wsi.h"

extern "C" void ps4_boot_trace(const char* stage);  // port/ps4_crashlog.cpp
extern "C" void ps4_profile_thread_begin();         // port/ps4_profile.cpp

void ssbu::ps4::wsi_log(const char* line) {
    std::fprintf(stderr, "%s\n", line);
}

// runtime/ps4/start.c, after the log and the working folder (/data/SSBU) are set up
extern "C" void ps4_port_start() {
    std::FILE* v = std::fopen("/app0/version.txt", "r");
    char version[16] = "?";
    if (v) {
        if (!std::fgets(version, sizeof version, v)) version[0] = 0;
        std::fclose(v);
    }
    for (char* c = version; *c; c++)
        if (*c == '\n' || *c == '\r') *c = 0;
    std::fprintf(stderr, "[boot] SSBU %s for PS4 (WindowSmash runtime, DXVK on RADV)\n", version);
    char line[64];
    std::snprintf(line, sizeof line, "SSBU %s starting", version);
    ps4_boot_trace(line);
    // DXVK: its log next to ours, its settings from /data/SSBU/dxvk.conf (written once, editable over FTP)
    setenv("DXVK_LOG_PATH", "/data/SSBU", 1);
    setenv("DXVK_LOG_LEVEL", "info", 0);
    setenv("DXVK_CONFIG_FILE", "/data/SSBU/dxvk.conf", 1);
    // its pipeline cache kept between runs (the first time each effect is drawn its pipeline is compiled: a hitch)
    mkdir("/data/SSBU/dxvk", 0777);
    setenv("DXVK_SHADER_CACHE_PATH", "/data/SSBU/dxvk", 1);
    // Pipelines only ever fast-linked from each shader's library: without this DXVK compiles every
    // pipeline a second time, whole and optimised, on its workers - on this CPU that kept two cores
    // busy for minutes once a match began. (dxvk.conf's lines come on top.)
    setenv("DXVK_CONFIG", "dxvk.enableGraphicsPipelineLibrary = True", 0);
    // the renderer's details (frame times, draws, uploads, shaders compiling) in the log every 5 s
    setenv("SSBU_LOG_STATS", "1", 0);
    ps4_profile_thread_begin();   // (the main thread: the others from their start)
    if (std::FILE* conf = std::fopen("/data/SSBU/dxvk.conf", "r")) {
        std::fclose(conf);
    } else if ((conf = std::fopen("/data/SSBU/dxvk.conf", "w"))) {
        std::fputs("# DXVK options for the SSBU PS4 port (https://github.com/doitsujin/dxvk/blob/master/dxvk.conf)\n"
                   "dxvk.numCompilerThreads = 2\n",
                   conf);
        std::fclose(conf);
    }
    std::fprintf(stderr, "[boot] VideoOut: %s\n", ssbu::ps4::video_out_open() ? "open" : "FAILED");
}

// port/ps4_crashlog.cpp's hook for the guest's state at a crash: the runtime writes its own report
// (/data/SSBU/reports/crash_*.txt) for faults in recompiled code.
extern "C" void ps4_crash_guest_info() {}
