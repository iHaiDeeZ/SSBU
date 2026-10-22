// The SSBU port's hooks into WindowSmash's PS4 runtime (runtime/ps4 in patches/windowsmash-ps4.patch) and into the
// PS4 layer (port/): its start (the screen opened before any graphics, as SMW's boot screen does; DXVK's settings),
// where the Vulkan WSI's lines go, and the crash log's guest part.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "../port/ps4_vk_wsi.h"

extern "C" void ps4_boot_trace(const char* stage);  // port/ps4_crashlog.cpp
extern "C" void ps4_profile_thread_begin();         // port/ps4_profile.cpp
extern "C" uint64_t ps4_new_thread_cores;            // port/ps4_runtime.cpp: the cores new threads start on
extern "C" void* scePthreadSelf();
extern "C" int scePthreadSetaffinity(void* thread, uint64_t mask);
extern "C" int scePthreadGetaffinity(void* thread, uint64_t* mask);

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
    // /data/SSBU/env.txt: NAME=value lines (# comments) set before anything reads them - the
    // runtime's switches (SSBU_*, DXVK_*) for a test run, changed over FTP without a new package
    if (std::FILE* env = std::fopen("/data/SSBU/env.txt", "r")) {
        char line[512];
        while (std::fgets(line, sizeof line, env)) {
            line[std::strcspn(line, "\r\n")] = 0;
            char* eq = std::strchr(line, '=');
            if (line[0] == '#' || !eq || eq == line)
                continue;
            *eq = 0;
            setenv(line, eq + 1, 1);
            std::fprintf(stderr, "[boot] env.txt: %s=%s\n", line, eq + 1);
        }
        std::fclose(env);
    }
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
    // Profiles only when asked for (L3+R3; SSBU_AUTO_PROFILE=1 in env.txt: also when the frame rate
    // drops, and a minute in): v00.13's two freezes (5 s, 409 s) each lasted exactly as long as the
    // profile running then, and v00.14 crashed seconds after one - its signal every 4 ms to every
    // game thread is not harmless while a match loads.
    setenv("SSBU_AUTO_PROFILE", "0", 0);
    // the game's clock (it reads it thousands of times a frame) straight from the CPU's counter, not
    // through QueryPerformanceCounter and clock_gettime: v00.09's profile had the guest main thread
    // spend a tenth of its time in those
    setenv("SSBU_CLOCK", "tsc", 0);
    // three shader compiler threads (the runtime's default here would be two: a third of the cores):
    // the game itself keeps one core busy and a little of two more, and a match's first draws wait
    // on hundreds of shaders
    setenv("SSBU_COMPILE_THREADS", "3", 0);
    // RADV's shader cache (scripts/build-mesa.sh): the GPU code of every shader, kept between runs - on
    // this CPU the driver takes about a second a shader, and a match's draws are skipped until theirs
    // are made. Mesa's single-file kind (one file and its index, appended to).
    mkdir("/data/SSBU/mesa", 0777);
    setenv("MESA_SHADER_CACHE_DIR", "/data/SSBU/mesa", 0);
    setenv("MESA_DISK_CACHE_SINGLE_FILE", "1", 0);
    // and the shader pack made on the PC (scripts/build-shaderpack.sh: every shader of the game, compiled by the same
    // RADV for this GPU), ssbu_pack.foz and ssbu_pack_idx.foz next to that cache's own files, read-only: a shader the
    // game meets for the first time is then read, not compiled. (Mesa leaves out a pack that is not there.)
    setenv("MESA_DISK_CACHE_READ_ONLY_FOZ_DBS", "ssbu_pack", 0);
    // The 7th core, when the system grants it (it shares it) and SSBU_SEVENTH_CORE=1 asks for it:
    // new threads may run on it too. Off: v00.14 crashed with it (the lazy memory race, fixed in
    // v00.17), and v00.20, on by default, froze in its first match with no freeze report.
    if (const char* want = std::getenv("SSBU_SEVENTH_CORE"); want && std::atoi(want) != 0) {
        void* self = scePthreadSelf();
        uint64_t had = 0, got = 0;
        scePthreadGetaffinity(self, &had);
        const bool seventh = scePthreadSetaffinity(self, 0x40) == 0 && scePthreadGetaffinity(self, &got) == 0 && got == 0x40;
        scePthreadSetaffinity(self, had);
        if (seventh)
            ps4_new_thread_cores = 0x7F;
        std::fprintf(stderr, "[boot] cores: %s; new threads on %#llx\n", seventh ? "7, the 7th granted" : "6 (the 7th refused)",
                     static_cast<unsigned long long>(ps4_new_thread_cores));
    } else {
        std::fprintf(stderr, "[boot] cores: 6 (SSBU_SEVENTH_CORE=1 for the 7th); new threads on %#llx\n",
                     static_cast<unsigned long long>(ps4_new_thread_cores));
    }
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
