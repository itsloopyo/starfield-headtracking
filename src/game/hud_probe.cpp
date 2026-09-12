#include "pch.h"
#include "hud_probe.h"

#include "core/logger.h"
#include "core/rtti_utils.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace StarfieldHT {

namespace {

using cameraunlock::memory::SafeRead;

struct Range { uintptr_t base; uintptr_t end; };

std::vector<Range> CommittedRanges() {
    std::vector<Range> ranges;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t address = 0x10000;
    constexpr uintptr_t kCeiling = 0x00007FFFFFFF0000ull;
    while (address < kCeiling) {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize == 0) break;
        address = base + mbi.RegionSize;
        if (mbi.State != MEM_COMMIT) continue;
        if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) continue;
        if (!ranges.empty() && ranges.back().end == base) ranges.back().end = address;
        else ranges.push_back({base, address});
    }
    return ranges;
}

bool InRanges(const std::vector<Range>& r, uintptr_t a, size_t n) {
    if (a < 0x10000) return false;
    auto it = std::upper_bound(r.begin(), r.end(), a,
                               [](uintptr_t v, const Range& x) { return v < x.base; });
    if (it == r.begin()) return false;
    --it;
    return a >= it->base && a + n <= it->end;
}

uintptr_t ReadWord(const std::vector<Range>& r, uintptr_t a) {
    if (!InRanges(r, a, sizeof(uintptr_t))) return 0;
    __try {
        return *reinterpret_cast<const uintptr_t*>(a);
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        return 0;
    }
}

// Anything in the module's writable sections that is, or points at, an object
// with `vtable` as its first word.
void FindInstances(uintptr_t moduleBase, size_t moduleSize, uintptr_t vtable,
                   const std::vector<Range>& ranges, std::vector<uintptr_t>& out) {
    constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY
                              | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const uintptr_t end = moduleBase + moduleSize;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t address = moduleBase;
    while (address < end) {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize == 0) break;
        address = base + mbi.RegionSize;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & kWritable) == 0) continue;
        if ((mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) continue;
        for (size_t i = 0; i < mbi.RegionSize / sizeof(uintptr_t); ++i) {
            const uintptr_t here = base + i * sizeof(uintptr_t);
            const uintptr_t word = ReadWord(ranges, here);
            uintptr_t instance = 0;
            if (word == vtable) instance = here;
            else if (word != 0 && (word & 7) == 0 && ReadWord(ranges, word) == vtable) instance = word;
            if (instance != 0 && std::find(out.begin(), out.end(), instance) == out.end()) {
                out.push_back(instance);
                if (out.size() >= 8) return;
            }
        }
    }
}

// Breadth-first walk of everything reachable from a root by following aligned
// pointers, reporting the path to any object whose RTTI names it. The menu the
// mod wants is held in a hash map inside the UI singleton, and walking to it
// costs less than matching the map's own layout.
void WalkFrom(const std::vector<Range>& ranges, uintptr_t root, const char* wanted) {
    struct Node { uintptr_t addr; int depth; };
    std::vector<Node> queue;
    // A hash set, not a vector: the budget is 40000 objects and 64 words each,
    // so a linear scan of the visited list is around 5e10 comparisons per walk -
    // minutes of a spinning core for a probe that should answer in seconds.
    std::unordered_set<uintptr_t> seen;
    queue.push_back({root, 0});
    seen.insert(root);
    size_t head = 0;
    int reported = 0;
    while (head < queue.size() && seen.size() < 40000 && reported < 6) {
        const Node node = queue[head++];
        for (int i = 0; i < 64; ++i) {
            const uintptr_t word = ReadWord(ranges, node.addr + i * 8);
            if (word == 0 || (word & 7) != 0) continue;
            if (!InRanges(ranges, word, 64)) continue;
            if (!seen.insert(word).second) continue;
            char name[160] = {};
            const uintptr_t vtable = ReadWord(ranges, word);
            if (vtable != 0 && GetClassNameFromVtable(vtable, name, sizeof(name))
                && strstr(name, wanted) != nullptr) {
                Logger::Instance().Info("HUD probe: %s found at 0x%llX, depth %d, via 0x%llX+0x%X",
                                        name, static_cast<unsigned long long>(word),
                                        node.depth + 1,
                                        static_cast<unsigned long long>(node.addr), i * 8);
                ++reported;
            }
            if (node.depth < 5) queue.push_back({word, node.depth + 1});
        }
    }
    Logger::Instance().Info("HUD probe: walk from 0x%llX visited %zu objects, %d hit(s) for \"%s\"",
                            static_cast<unsigned long long>(root), seen.size(), reported, wanted);
}

void DumpObject(const std::vector<Range>& ranges, uintptr_t object, const char* label, int words) {
    Logger::Instance().Info("--- %s at 0x%llX ---", label,
                            static_cast<unsigned long long>(object));
    for (int i = 0; i < words; ++i) {
        const uintptr_t word = ReadWord(ranges, object + i * 8);
        if (word == 0) continue;
        char name[160] = {};
        const uintptr_t inner = ReadWord(ranges, word);
        if (inner != 0 && !GetClassNameFromVtable(inner, name, sizeof(name))) name[0] = 0;
        if (name[0] == 0) continue;
        Logger::Instance().Info("  +0x%03X -> 0x%llX %s", i * 8,
                                static_cast<unsigned long long>(word), name);
    }
}

} // namespace

void ProbeHud() {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!gameModule) return;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) return;

    const std::vector<Range> ranges = CommittedRanges();
    const char* classes[] = {".?AVUI@@", ".?AVHUDMenu@@", ".?AVMovieImpl@GFx@Scaleform@@"};
    for (const char* className : classes) {
        const uintptr_t vtable = FindVtableByRTTI(moduleBase, moduleSize, className);
        if (vtable == 0) {
            Logger::Instance().Info("HUD probe: no vtable for %s", className);
            continue;
        }
        std::vector<uintptr_t> found;
        FindInstances(moduleBase, moduleSize, vtable, ranges, found);
        Logger::Instance().Info("HUD probe: %s vtable RVA 0x%llX, %zu instance(s) from module data",
                                className, static_cast<unsigned long long>(vtable - moduleBase),
                                found.size());
        for (uintptr_t instance : found) {
            DumpObject(ranges, instance, className, 48);
            if (strcmp(className, ".?AVUI@@") == 0) {
                WalkFrom(ranges, instance, "HUDMenu");
                WalkFrom(ranges, instance, "Movie");
            }
        }
    }
}

} // namespace StarfieldHT
