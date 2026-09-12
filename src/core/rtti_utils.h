#pragma once

#include <cstddef>
#include <cstdint>

namespace StarfieldHT {

// MSVC RTTI CompleteObjectLocator layout (32-bit RVAs, x64 image).
struct RTTICompleteObjectLocator {
    uint32_t signature;
    uint32_t offset;
    uint32_t cdOffset;
    uint32_t pTypeDescriptor;
    uint32_t pClassDescriptor;
    uint32_t pSelf;
};

// Discover a class's vtable by walking RTTI structures in the game module:
// locate the type's CompleteObjectLocator, then the vtable that references it.
// Returns the address of vtable slot 0, or 0 if discovery fails (logged).
uintptr_t FindVtableByRTTI(uintptr_t moduleBase, size_t moduleSize, const char* className);

// The decorated class name behind an object's vtable, e.g. ".?AVFirstPersonState@@".
// False when the pointer does not lead to a CompleteObjectLocator in the module,
// which is the answer for anything that is not a live polymorphic game object.
bool GetClassNameFromVtable(uintptr_t vtable, char* out, size_t outSize);

} // namespace StarfieldHT
