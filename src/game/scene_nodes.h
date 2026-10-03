#pragma once

#include <cstddef>
#include <cstdint>

namespace StarfieldHT::SceneNodes {

inline constexpr uint16_t kMaxChildren = 64;

bool IsObject(uintptr_t p);

// True when the node's name begins with the `size` bytes at `text`. Pass a
// string literal's sizeof to match the whole name, terminator included, or one
// less to match a prefix.
bool NameStartsWith(uintptr_t node, const char* text, size_t size);

bool ParentIs(uintptr_t node, uintptr_t parent);

// A leaf that is not a node has no children array where a node keeps one, so
// every child is checked against its own parent pointer before it is returned.
uint16_t ReadChildren(uintptr_t node, uintptr_t out[kMaxChildren]);

// The player skeleton's Camera bone, which sits exactly on the camera's eye, or
// 0 while the player has no 3D loaded.
uintptr_t ReadCameraBone();

} // namespace StarfieldHT::SceneNodes
