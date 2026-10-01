#include "pch.h"
#include "rtti_utils.h"

#include "core/constants.h"
#include "core/logger.h"
#include "game/build_selection.h"

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

// The two scans below walk one readable run each, from NextReadableRange, and
// carry their own SEH frame: the run was readable when it was measured, but a
// protection change can still race the scan, and losing that race must not
// close the game. A fault ends the run's scan with no match.
uintptr_t ScanRunForCol(uintptr_t moduleBase, uintptr_t runBase, size_t runSize, uint32_t typeDescRva) {
    __try {
        for (size_t off = 0; off + sizeof(RTTICompleteObjectLocator) <= runSize; off += 4) {
            const uintptr_t addr = runBase + off;
            const auto* candidate = reinterpret_cast<const RTTICompleteObjectLocator*>(addr);
            if (candidate->signature == 1 &&
                candidate->offset == 0 &&
                candidate->pTypeDescriptor == typeDescRva &&
                candidate->pSelf == static_cast<uint32_t>(addr - moduleBase)) {
                return addr;
            }
        }
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
    }
    return 0;
}

uintptr_t ScanRunForVtable(uintptr_t moduleBase, uintptr_t runBase, size_t runSize, uintptr_t colAddr) {
    // Runs start on page boundaries, so 8-byte steps from the run base are
    // 8-byte aligned in the image too.
    __try {
        for (size_t off = 0; off + sizeof(uintptr_t) <= runSize; off += sizeof(uintptr_t)) {
            const uintptr_t addr = runBase + off;
            if (*reinterpret_cast<const uintptr_t*>(addr) != colAddr) continue;
            if (!IsInReadOnlyData(moduleBase, static_cast<uint32_t>(addr - moduleBase))) continue;
            return addr + sizeof(uintptr_t);
        }
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
    }
    return 0;
}

} // namespace

// Walked through NextReadableRange rather than straight across SizeOfImage: an
// image can map a section PAGE_NOACCESS, and a raw read there closes the game.
uintptr_t FindVtableByRTTI(uintptr_t moduleBase, size_t moduleSize, const char* className) {
    if (const auto* contracts = RuntimeContracts()) {
        const auto found = contracts->classes.find(className);
        if (found == contracts->classes.end()) {
            Logger::Instance().Error("No validated runtime type for %s", className);
            return 0;
        }
        return moduleBase + found->second;
    }
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);

    void* typeDesc = cameraunlock::memory::FindRTTIDescriptor(gameModule, className);
    if (!typeDesc) {
        Logger::Instance().Error("RTTI TypeDescriptor not found for: %s", className);
        return 0;
    }

    const uint32_t typeDescRVA =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(typeDesc) - moduleBase);
    const uintptr_t moduleEnd = moduleBase + moduleSize;
    uintptr_t cursor = moduleBase, runBase = 0;
    size_t runSize = 0;

    uintptr_t colAddr = 0;
    while (colAddr == 0 && cameraunlock::memory::NextReadableRange(cursor, moduleEnd, runBase, runSize)) {
        colAddr = ScanRunForCol(moduleBase, runBase, runSize, typeDescRVA);
    }
    if (colAddr == 0) {
        Logger::Instance().Error("CompleteObjectLocator not found for: %s", className);
        return 0;
    }

    cursor = moduleBase;
    while (cameraunlock::memory::NextReadableRange(cursor, moduleEnd, runBase, runSize)) {
        const uintptr_t vtable = ScanRunForVtable(moduleBase, runBase, runSize, colAddr);
        if (vtable != 0) return vtable;
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
