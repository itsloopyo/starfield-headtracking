#include "pch.h"
#include "hotkey_utils.h"

#include <cstring>
#include <cameraunlock/input/hotkey_poller.h>

namespace StarfieldHT {

const char* VirtualKeyToString(int vkCode) {
    const char* result = cameraunlock::input::VirtualKeyToString(vkCode);
    if (result && std::strcmp(result, "Unknown") != 0) {
        return result;
    }

    switch (vkCode) {
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x14: return "CapsLock";
        case 0x10: return "Shift";
        case 0x11: return "Ctrl";
        case 0x12: return "Alt";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: return result;
    }
}

} // namespace StarfieldHT
