#pragma once

#include <cstddef>
#include <cstdint>

namespace StarfieldHT {

// Finds live instances of a game class by scanning committed memory for the
// vtable pointer its objects carry.
//
// Some of what a mod needs is not reachable by walking pointers from anything
// it already holds: a HUD widget is owned by the UI, which is owned by a
// singleton the mod has no address for. The objects themselves are findable
// though, because every polymorphic one starts with a pointer to a vtable that
// RTTI can name.
//
// Developer aid. Nothing in a shipped build calls this.
//
// `className` is the decorated RTTI name, e.g. ".?AVHUDMenu@@". Each instance
// found is logged with a dump of its first `dumpBytes` bytes as floats and as
// pointers, up to `maxInstances`.
void DumpInstancesByRtti(const char* className, int maxInstances, size_t dumpBytes);

} // namespace StarfieldHT
