#include "pch.h"
#include "rtti_utils.h"

#include "core/constants.h"
#include "core/logger.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

#include <cstring>

namespace StarfieldHT {

namespace {

// True when `rva` lands in a section that is readable, not writable and not
// executable - which is where vtables and their locators live. The reference
// scan needs it because it looks for one qword value across the whole image and
// takes the first hit: a relocated pointer in .data holding the same address
// would otherwise be accepted as a vtable, and the mod would hook slot N of
// something that is not one and crash on the first dispatch. That is the
// outcome the build fingerprint exists to prevent, reached around the side.
bool IsInReadOnlyData(uintptr_t moduleBase, uint32_t rva) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const uint32_t start = section->VirtualAddress;
        const uint32_t end = start + section->Misc.VirtualSize;
        if (rva < start || rva >= end) continue;
        const DWORD flags = section->Characteristics;
        return (flags & IMAGE_SCN_MEM_READ) != 0
            && (flags & IMAGE_SCN_MEM_WRITE) == 0
            && (flags & IMAGE_SCN_MEM_EXECUTE) == 0;
    }
    return false;
}

} // namespace

uintptr_t FindVtableByRTTI(uintptr_t moduleBase, size_t moduleSize, const char* className) {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);

    void* typeDesc = cameraunlock::memory::FindRTTIDescriptor(gameModule, className);
    if (!typeDesc) {
        Logger::Instance().Error("RTTI TypeDescriptor not found for: %s", className);
        return 0;
    }

    const uint32_t typeDescRVA =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(typeDesc) - moduleBase);
    const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(moduleBase);
    uintptr_t colAddr = 0;

    for (size_t i = 0; i + sizeof(RTTICompleteObjectLocator) <= moduleSize; i += 4) {
        const auto* candidate = reinterpret_cast<const RTTICompleteObjectLocator*>(scanStart + i);
        if (candidate->signature == 1 &&
            candidate->offset == 0 &&
            candidate->pTypeDescriptor == typeDescRVA &&
            candidate->pSelf == static_cast<uint32_t>(i)) {
            colAddr = moduleBase + i;
            break;
        }
    }

    if (colAddr == 0) {
        Logger::Instance().Error("CompleteObjectLocator not found for: %s", className);
        return 0;
    }

    for (size_t i = 0; i + 8 <= moduleSize; i += 8) {
        if (*reinterpret_cast<const uintptr_t*>(scanStart + i) != colAddr) continue;
        if (!IsInReadOnlyData(moduleBase, static_cast<uint32_t>(i))) continue;
        return moduleBase + i + 8;
    }

    Logger::Instance().Error("Vtable reference not found for: %s", className);
    return 0;
}

bool GetClassNameFromVtable(uintptr_t vtable, char* out, size_t outSize) {
    if (vtable == 0 || out == nullptr || outSize == 0) return false;

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!gameModule) return false;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) return false;

    const auto inModule = [&](uintptr_t p) {
        return p >= moduleBase && p < moduleBase + moduleSize;
    };

    uintptr_t colAddr = 0;
    if (!cameraunlock::memory::SafeRead(vtable - sizeof(uintptr_t), colAddr)) return false;
    if (!inModule(colAddr)) return false;

    RTTICompleteObjectLocator col{};
    if (!cameraunlock::memory::SafeRead(colAddr, col)) return false;
    if (col.signature != 1) return false;

    const uintptr_t typeDesc = moduleBase + col.pTypeDescriptor;
    if (!inModule(typeDesc)) return false;

    // TypeDescriptor is {void* vftable, void* spare, char name[]}.
    char name[192] = {};
    if (!cameraunlock::memory::SafeRead(typeDesc + 16, name)) return false;
    name[sizeof(name) - 1] = 0;
    if (name[0] != '.') return false;

    strncpy(out, name, outSize - 1);
    out[outSize - 1] = 0;
    return true;
}

} // namespace StarfieldHT
