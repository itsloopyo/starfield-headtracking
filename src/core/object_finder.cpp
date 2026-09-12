#include "pch.h"
#include "object_finder.h"

#include "core/logger.h"
#include "core/rtti_utils.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {

namespace {

using cameraunlock::memory::SafeRead;

// A region worth searching: committed, writable, private to the process. Game
// objects live in the heap; the module's own sections and mapped files hold
// vtables and assets, not instances.
bool IsSearchable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Type != MEM_PRIVATE) return false;
    if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) return false;
    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY
                         | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & writable) != 0;
}

void DumpInstance(uintptr_t obj, size_t dumpBytes) {
    Logger::Instance().Info("  instance 0x%llX", static_cast<unsigned long long>(obj));
    for (size_t o = 0; o + 32 <= dumpBytes; o += 32) {
        float f[8] = {};
        if (!SafeRead(obj + o, f)) return;
        Logger::Instance().Info(
            "    +0x%03llX f: %+.3f %+.3f %+.3f %+.3f %+.3f %+.3f %+.3f %+.3f",
            static_cast<unsigned long long>(o),
            f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
    }
}

} // namespace

void DumpInstancesByRtti(const char* className, int maxInstances, size_t dumpBytes) {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!gameModule) return;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) return;

    const uintptr_t vtable = FindVtableByRTTI(moduleBase, moduleSize, className);
    if (vtable == 0) {
        Logger::Instance().Error("Object finder: no vtable for %s", className);
        return;
    }
    Logger::Instance().Info("=== instances of %s (vtable RVA 0x%llX) ===", className,
                            static_cast<unsigned long long>(vtable - moduleBase));

    int found = 0;
    uint64_t bytesScanned = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t address = 0x10000;
    constexpr uintptr_t kAddressCeiling = 0x00007FFFFFFF0000ull;
    // Enough to cover the heaps without the scan itself becoming the problem:
    // a stall long enough to trip the game's own watchdog would be a worse
    // diagnostic than no answer at all.
    constexpr uint64_t kScanBudget = 3ull * 1024 * 1024 * 1024;

    while (address < kAddressCeiling && found < maxInstances && bytesScanned < kScanBudget) {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const size_t regionSize = mbi.RegionSize;
        address = regionBase + regionSize;

        if (!IsSearchable(mbi)) continue;
        bytesScanned += regionSize;

        __try {
            const uintptr_t* p = reinterpret_cast<const uintptr_t*>(regionBase);
            const size_t count = regionSize / sizeof(uintptr_t);
            for (size_t i = 0; i < count; ++i) {
                if (p[i] != vtable) continue;
                DumpInstance(regionBase + i * sizeof(uintptr_t), dumpBytes);
                if (++found >= maxInstances) break;
            }
        } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
            // A region's protection can change under the scan; skip it.
        }
    }

    Logger::Instance().Info("=== %d instance(s) of %s, %llu MB scanned ===", found, className,
                            static_cast<unsigned long long>(bytesScanned / (1024 * 1024)));
}

} // namespace StarfieldHT
