#pragma once

#include <string>
#include <Windows.h>

namespace StarfieldHT {

// Get the directory containing our DLL
std::string GetModuleDirectory();

// Get full path to a file in the same directory as our DLL
std::string GetModulePath(const char* filename);

} // namespace StarfieldHT
