// Sampling profiler (SSBU): while a profile runs, the game's threads (each registers at its start:
// ps4_runtime.cpp's pthread_create wrap) are interrupted with SIGPROF every few milliseconds and the
// handler notes the interrupted instruction, and whether it sits right after a `syscall` (the thread
// is waiting in the kernel). Signals go to all of them at once and the handlers write into their own
// thread's slot, so a round costs the sampler a few microseconds a thread.
//
// Only threads named as the runtime's and DXVK's own (kSampled): on SMW a signal sampler froze the
// game after ~40 s, a thread that waits on the GPU stuck in a libkernel wait it interrupted. These
// wait only on mutexes and condition variables, which take an interruption (libthr starts the wait
// again); the GPU submission, display and audio threads are left alone. And only for a few seconds.
//
// A profile is started by the runtime (runtime/ps4/profile.c: when the frame rate drops, and L3+R3)
// and written to /data/SSBU/profiles/profile_<n>.txt: per thread its samples, its busy share and its
// busiest addresses, as "elf 0x.." (relative to .text, as crash.log), "mod <module>+0x.." (system
// modules) or "abs 0x.." (code loaded at run time: the NROs, whose places ssbu.log's [ro] lines
// give). scripts/profile.py names them and adds them up by function.

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>

extern "C" char __text_start[];                // the OpenOrbis link.x: the start of .text
extern "C" int pthread_getthreadid_np(void);   // libkernel (FreeBSD libthr)
extern "C" int scePthreadGetname(pthread_t thread, char* name);
extern "C" int __real_pthread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
extern "C" const char* rt_thread_description(int tid);   // the runtime's (win32.c): its whole name

namespace {

constexpr int kSigProf = 27;   // FreeBSD
constexpr int kSaOnstack = 0x0001, kSaRestart = 0x0002, kSaSiginfo = 0x0040;
constexpr int kMcontextOffset = 0x40, kRipIndex = 20;   // measured on hardware (ps4_crashlog.cpp)
constexpr uint64_t kSystemLibs = 0x800000000ULL, kSystemEnd = 0x1000000000ULL;
constexpr uintptr_t kTextWindow = 0x40000000;
constexpr uint64_t kWaitingTag = 1ULL << 63;
constexpr int kSlots = 256, kRing = 2048;   // threads alive at once; samples a thread per profile
constexpr long kPeriodNs = 4000000;         // a round every 4 ms

enum : uint32_t { kFree, kAlive, kEnded };   // kEnded: the thread is gone, its samples not yet written

// The threads sampled, by the start of their names (the runtime's SetThreadDescription, cut to 15
// characters by the system; DXVK's setThreadName).
const char* const kSampled[] = {"guest", "gpu", "vsync", "shader compiler", "status", "window",
                                "dxvk-cs", "dxvk-shader", "dxvk-descriptor"};

struct Slot {
    std::atomic<uint32_t> state{kFree};
    std::atomic<pthread_t> thread{};
    int tid = 0;
    bool checked = false, sampled = false;   // (this profile: its name looked at)
    char name[64] = {};
    std::atomic<uint32_t> count{0};
    uint64_t samples[kRing];
};
Slot g_slots[kSlots];
std::atomic<bool> g_running{false}, g_busy{false};   // busy: from the start until it is written
std::atomic<int> g_profiles{0};
pthread_key_t g_end_key;   // its destructor: the thread's end (also by pthread_exit)
__thread int t_slot __attribute__((tls_model("initial-exec"))) = -1;

void onProf(int, siginfo_t*, void* context) {
    const int i = t_slot;
    if (i < 0 || !g_running.load(std::memory_order_relaxed))
        return;
    Slot& s = g_slots[i];
    const auto* regs = reinterpret_cast<const uint64_t*>(static_cast<char*>(context) + kMcontextOffset);
    uint64_t rip = regs[kRipIndex];
    // in a system module, right after a `syscall` (0f 05): waiting in the kernel
    if (rip >= kSystemLibs && rip < kSystemEnd) {
        const auto* code = reinterpret_cast<const uint8_t*>(rip);
        if ((rip & 0xFFF) >= 2 && code[-2] == 0x0F && code[-1] == 0x05)
            rip |= kWaitingTag;
    }
    const uint32_t n = s.count.load(std::memory_order_relaxed);
    if (n < kRing) {
        s.samples[n] = rip;
        s.count.store(n + 1, std::memory_order_release);
    }
}

void installHandler() {
    static std::atomic<bool> installed{false};
    if (installed.exchange(true))
        return;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = onProf;
    action.sa_flags = kSaSiginfo | kSaRestart | kSaOnstack;
    sigemptyset(&action.sa_mask);
    sigaction(kSigProf, &action, nullptr);
}

double nowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

// Loaded system modules and their segments, so their samples can be named by module offset.
struct ModuleSegment {
    void* address;
    uint32_t size;
    int32_t prot;
};
struct ModuleInfo {
    size_t size;
    char name[256];
    ModuleSegment segments[4];
    uint32_t segment_count;
    uint8_t fingerprint[20];
};
extern "C" int32_t sceKernelGetModuleList(int32_t* handles, size_t count, size_t* available);
extern "C" int32_t sceKernelGetModuleInfo(int32_t handle, ModuleInfo* info);

struct Module {
    uint64_t start, end;
    char name[48];
};
Module g_modules[96];
int g_module_count = 0;

void listModules() {
    int32_t handles[96];
    size_t count = 0;
    g_module_count = 0;
    if (sceKernelGetModuleList(handles, 96, &count) != 0)
        return;
    for (size_t m = 0; m < count && m < 96; m++) {
        ModuleInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetModuleInfo(handles[m], &info) != 0 || !info.segment_count)
            continue;
        Module& out = g_modules[g_module_count++];
        out.start = reinterpret_cast<uint64_t>(info.segments[0].address);
        out.end = out.start + info.segments[0].size;   // (the first segment: its code)
        snprintf(out.name, sizeof(out.name), "%s", info.name);
    }
}

// An address as the report writes it.
void describe(uint64_t rip, char* out, size_t n) {
    const uintptr_t text = reinterpret_cast<uintptr_t>(__text_start);
    if (rip >= text && rip < text + kTextWindow) {
        snprintf(out, n, "elf 0x%llx", static_cast<unsigned long long>(rip - text));
        return;
    }
    for (int m = 0; m < g_module_count; m++) {
        if (rip >= g_modules[m].start && rip < g_modules[m].end) {
            snprintf(out, n, "mod %s+0x%llx", g_modules[m].name, static_cast<unsigned long long>(rip - g_modules[m].start));
            return;
        }
    }
    snprintf(out, n, "abs 0x%llx", static_cast<unsigned long long>(rip));
}

struct Entry {
    uint64_t rip;
    uint32_t count;
};
uint64_t g_sorted[kRing];
Entry g_entries[kRing];

void writeThread(FILE* f, Slot& s, uint32_t rounds) {
    const uint32_t n = std::min<uint32_t>(s.count.load(std::memory_order_acquire), kRing);
    if (!n)
        return;
    uint32_t waiting = 0;
    for (uint32_t i = 0; i < n; i++) {
        g_sorted[i] = s.samples[i];
        waiting += (g_sorted[i] & kWaitingTag) != 0;
    }
    std::sort(g_sorted, g_sorted + n);
    uint32_t unique = 0;
    for (uint32_t i = 0; i < n;) {
        uint32_t j = i;
        while (j < n && g_sorted[j] == g_sorted[i])
            j++;
        g_entries[unique++] = {g_sorted[i], j - i};
        i = j;
    }
    std::sort(g_entries, g_entries + unique, [](const Entry& a, const Entry& b) { return a.count > b.count; });
    fprintf(f, "thread %d \"%s\": %u samples of %u rounds, %.1f%% busy\n", s.tid, s.name, n, rounds,
            100.0 * (n - waiting) / n);
    char where[96];
    for (uint32_t i = 0; i < unique && i < 400; i++) {
        if (g_entries[i].count < 2 && i >= 40)
            break;
        describe(g_entries[i].rip & ~kWaitingTag, where, sizeof(where));
        fprintf(f, "  %5u %s%s\n", g_entries[i].count, where, (g_entries[i].rip & kWaitingTag) ? " wait" : "");
    }
}

struct Run {
    double seconds;
    char reason[96];
};

// A live thread's name (the runtime's whole description, else the system's) and whether it is one to sample.
void check(Slot& s) {
    s.checked = true;
    const char* description = rt_thread_description(s.tid);
    if (description && *description) {
        snprintf(s.name, sizeof(s.name), "%s", description);
    } else {
        char name[40] = {};
        scePthreadGetname(s.thread.load(), name);
        snprintf(s.name, sizeof(s.name), "%s", name);
    }
    s.sampled = false;
    for (const char* prefix : kSampled)
        s.sampled |= strncmp(s.name, prefix, strlen(prefix)) == 0;
}

void* profileThread(void* raw) {
    Run run = *static_cast<Run*>(raw);
    delete static_cast<Run*>(raw);
    for (Slot& s : g_slots) {
        s.count.store(0);
        s.checked = s.sampled = false;
    }
    g_running.store(true);
    const double start = nowSeconds();
    uint32_t rounds = 0;
    const timespec period = {0, kPeriodNs};
    while (nowSeconds() - start < run.seconds) {
        const bool look = rounds % 25 == 0;   // (new threads, every 100 ms)
        for (Slot& s : g_slots) {
            if (s.state.load(std::memory_order_acquire) != kAlive)
                continue;
            if (look && !s.checked)
                check(s);
            if (s.sampled)
                pthread_kill(s.thread.load(std::memory_order_relaxed), kSigProf);
        }
        rounds++;
        nanosleep(&period, nullptr);
    }
    g_running.store(false);
    const double took = nowSeconds() - start;
    nanosleep(&period, nullptr);   // (handlers still running finish)

    const int number = g_profiles.fetch_add(1) + 1;
    mkdir("/data/SSBU/profiles", 0777);
    char path[96];
    snprintf(path, sizeof(path), "/data/SSBU/profiles/profile_%d.txt", number);
    FILE* f = fopen(path, "w");
    if (f) {
        listModules();
        fprintf(f, "profile %d: %s; %.1f s, %u rounds (every %ld ms); text at %p\n", number, run.reason, took, rounds,
                kPeriodNs / 1000000, static_cast<void*>(__text_start));
        for (int m = 0; m < g_module_count; m++)
            fprintf(f, "module %s 0x%llx-0x%llx\n", g_modules[m].name, static_cast<unsigned long long>(g_modules[m].start),
                    static_cast<unsigned long long>(g_modules[m].end));
        uint32_t left_alone = 0;
        for (Slot& s : g_slots) {
            if (s.state.load() == kFree || !s.checked)
                continue;
            if (s.sampled)
                writeThread(f, s, rounds);
            else
                fprintf(f, "%s thread %d \"%s\"\n", left_alone++ ? "     " : "not sampled:", s.tid, s.name);
        }
        fclose(f);
    }
    for (Slot& s : g_slots) {
        uint32_t ended = kEnded;
        s.state.compare_exchange_strong(ended, kFree);
        s.count.store(0);
    }
    fprintf(stderr, "[profile] %s: %.1f s sampled, written to %s\n", run.reason, took, path);
    g_busy.store(false);
    return nullptr;
}

void threadEnded(void* value) {
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(value)) - 1;
    t_slot = -1;
    if (i < 0 || i >= kSlots)
        return;
    Slot& s = g_slots[i];
    // during a profile: its samples are written with it
    s.state.store(g_busy.load() && s.checked ? kEnded : kFree);
}

}  // namespace

// (each thread, at its start: ps4_runtime.cpp; the main thread: ssbu_port.cpp)
extern "C" void ps4_profile_thread_begin() {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, [] {
        pthread_key_create(&g_end_key, threadEnded);
        installHandler();
    });
    if (t_slot >= 0)
        return;
    for (int i = 0; i < kSlots; i++) {
        uint32_t expected = kFree;
        if (g_slots[i].state.load(std::memory_order_relaxed) == kFree &&
            g_slots[i].state.compare_exchange_strong(expected, kAlive)) {
            g_slots[i].count.store(0);
            g_slots[i].name[0] = 0;
            g_slots[i].checked = g_slots[i].sampled = false;
            g_slots[i].tid = pthread_getthreadid_np();
            g_slots[i].thread.store(pthread_self());
            t_slot = i;
            pthread_setspecific(g_end_key, reinterpret_cast<void*>(static_cast<intptr_t>(i) + 1));
            return;
        }
    }
}

extern "C" int ps4_profile_running() {
    return g_busy.load();
}

// A profile of `seconds`, in a thread of its own; false while one runs.
extern "C" bool ps4_profile_start(double seconds, const char* reason) {
    if (g_busy.exchange(true))
        return false;
    auto* run = new Run{seconds, {}};
    snprintf(run->reason, sizeof(run->reason), "%s", reason ? reason : "?");
    pthread_t thread;
    // (the system's own: the profiler's thread is no thread of the app's to sample)
    const bool ok = __real_pthread_create(&thread, nullptr, [](void* r) -> void* {
        profileThread(r);
        return nullptr;
    }, run) == 0;
    if (ok) {
        pthread_detach(thread);
    } else {
        delete run;
        g_busy.store(false);
    }
    return ok;
}
