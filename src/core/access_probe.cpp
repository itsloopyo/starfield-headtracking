#include "pch.h"
#include "access_probe.h"

#include "core/logger.h"

#include <cameraunlock/memory/pattern_scanner.h>

namespace StarfieldHT {

namespace {

constexpr int kMaxDistinct = 96;

PVOID     g_handler = nullptr;
uintptr_t g_pageBase = 0;
// The watched range can straddle a page boundary - a 64-byte transform starting
// at +0xFE0 does - and guarding only the first page reports "nothing writes
// this" for a range half of which was never armed.
size_t    g_pageBytes = 0;
uintptr_t g_watchLo = 0;
uintptr_t g_watchHi = 0;
uintptr_t g_moduleBase = 0;
size_t    g_moduleSize = 0;
std::atomic<int> g_hitsLeft{0};
std::atomic<bool> g_armed{false};

struct Site {
    uintptr_t rva;
    uint32_t  count;
    bool      wrote;
};
Site g_sites[kMaxDistinct] = {};
int  g_siteCount = 0;
CRITICAL_SECTION g_sitesLock;
bool g_lockReady = false;

void RecordSite(uintptr_t rva, bool wrote) {
    EnterCriticalSection(&g_sitesLock);
    for (int i = 0; i < g_siteCount; ++i) {
        if (g_sites[i].rva == rva) {
            ++g_sites[i].count;
            g_sites[i].wrote = g_sites[i].wrote || wrote;
            LeaveCriticalSection(&g_sitesLock);
            return;
        }
    }
    if (g_siteCount < kMaxDistinct) {
        g_sites[g_siteCount].rva = rva;
        g_sites[g_siteCount].count = 1;
        g_sites[g_siteCount].wrote = wrote;
        ++g_siteCount;
    }
    LeaveCriticalSection(&g_sitesLock);
}

void ReportSites() {
    EnterCriticalSection(&g_sitesLock);
    Logger::Instance().Info("=== access probe: %d distinct sites touched the camera page ===",
                            g_siteCount);
    for (int i = 0; i < g_siteCount; ++i) {
        Logger::Instance().Info("  RVA 0x%llX  x%u  %s",
                                static_cast<unsigned long long>(g_sites[i].rva),
                                g_sites[i].count, g_sites[i].wrote ? "WRITE" : "read");
    }
    Logger::Instance().Info("=== end access probe ===");
    LeaveCriticalSection(&g_sitesLock);
}

// Re-arming from inside the handler is not possible: the guard is already gone
// by the time we run, and VirtualProtect here would fire again on the very next
// instruction of our own logging. Instead the single-step flag carries us one
// instruction past the access, and the step exception re-arms the page.
//
// Per thread, because the trap flag it pairs with is set in a per-thread context
// record. The game faults on the camera page from several threads, and one
// process-wide flag meant two threads arming their trap flags before either
// stepped: the first step consumed the flag, the second thread's step then found
// nothing to pair with and returned CONTINUE_SEARCH for a single-step nobody
// else handles, which kills the process.
thread_local bool t_pendingRearm = false;

LONG CALLBACK GuardHandler(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;

    if (code == STATUS_GUARD_PAGE_VIOLATION) {
        const uintptr_t rip = static_cast<uintptr_t>(info->ContextRecord->Rip);
        const uintptr_t touched =
            info->ExceptionRecord->NumberParameters >= 2
                ? static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1])
                : 0;
        const bool ofInterest = touched >= g_watchLo && touched < g_watchHi;
        const bool wrote = info->ExceptionRecord->NumberParameters >= 1
                        && info->ExceptionRecord->ExceptionInformation[0] != 0;
        if (ofInterest && rip >= g_moduleBase && rip < g_moduleBase + g_moduleSize) {
            RecordSite(rip - g_moduleBase, wrote);
        }
        if (!ofInterest || g_hitsLeft.fetch_sub(1, std::memory_order_relaxed) > 1) {
            info->ContextRecord->EFlags |= 0x100;  // trap flag: step one instruction
            t_pendingRearm = true;
        } else {
            g_armed.store(false, std::memory_order_release);
            ReportSites();
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if (code == EXCEPTION_SINGLE_STEP && t_pendingRearm) {
        t_pendingRearm = false;
        // A thread can reach here after DisarmAccessProbe has cleared the guard,
        // and re-applying it would restart a probe that was told to stop.
        if (g_armed.load(std::memory_order_acquire)) {
            DWORD old = 0;
            VirtualProtect(reinterpret_cast<LPVOID>(g_pageBase), g_pageBytes,
                           PAGE_READWRITE | PAGE_GUARD, &old);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

bool ArmAccessProbe(uintptr_t address, size_t length, int maxHits) {
    if (g_armed.load(std::memory_order_acquire)) {
        Logger::Instance().Info("Access probe already armed");
        return false;
    }
    if (address == 0) return false;

    if (!g_lockReady) {
        InitializeCriticalSection(&g_sitesLock);
        g_lockReady = true;
    }

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) return false;
    if (!cameraunlock::memory::GetModuleRange(gameModule, g_moduleBase, g_moduleSize)) return false;

    EnterCriticalSection(&g_sitesLock);
    g_siteCount = 0;
    LeaveCriticalSection(&g_sitesLock);

    g_pageBase = address & ~static_cast<uintptr_t>(0xFFF);
    g_watchLo = address;
    g_watchHi = address + length;
    g_pageBytes = ((g_watchHi - g_pageBase) + 0xFFF) & ~static_cast<uintptr_t>(0xFFF);
    g_hitsLeft.store(maxHits, std::memory_order_relaxed);

    if (!g_handler) {
        g_handler = AddVectoredExceptionHandler(1, &GuardHandler);
        if (!g_handler) {
            Logger::Instance().Error("Access probe: AddVectoredExceptionHandler failed");
            return false;
        }
    }

    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(g_pageBase), g_pageBytes,
                        PAGE_READWRITE | PAGE_GUARD, &old)) {
        Logger::Instance().Error("Access probe: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }

    g_armed.store(true, std::memory_order_release);
    Logger::Instance().Info("Access probe armed: page 0x%llX watching 0x%llX..0x%llX for %d hits",
                            static_cast<unsigned long long>(g_pageBase),
                            static_cast<unsigned long long>(g_watchLo),
                            static_cast<unsigned long long>(g_watchHi), maxHits);
    return true;
}

void DisarmAccessProbe() {
    if (!g_armed.exchange(false, std::memory_order_acq_rel)) return;
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(g_pageBase), g_pageBytes, PAGE_READWRITE, &old);
    // The handler stays installed. Removing it here is a one-instruction race
    // against any thread that has taken a guard fault and set its trap flag but
    // not yet stepped: that thread's single-step then finds nobody to handle it
    // and kills the process. Two compares per exception for the rest of the
    // session is the cheaper side of that trade.
    ReportSites();
}

} // namespace StarfieldHT
