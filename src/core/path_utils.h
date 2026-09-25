#pragma once

#include <string>
#include <Windows.h>

namespace StarfieldHT {

// Get the directory containing our DLL
std::string GetModuleDirectory();

// Get full path to a file in the same directory as our DLL
std::string GetModulePath(const char* filename);

// The same as a wide string, so a folder the ANSI code page cannot spell is still found.
// Empty when the module's own path cannot be read.
std::wstring GetModulePathW(const wchar_t* filename);

} // namespace StarfieldHT
