#pragma once

#include <string>
#include <Windows.h>

namespace StarfieldHT {

// Get the directory containing our DLL
std::string GetModuleDirectory();

// Get full path to a file in the same directory as our DLL
std::string GetModulePath(const char* filename);

// The directory containing our DLL, ending in its separator, as a wide string so a folder the
// ANSI code page cannot spell is still found. Empty when the module's own path cannot be read.
std::wstring GetModuleDirectoryW();

} // namespace StarfieldHT
