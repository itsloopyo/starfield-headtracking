#pragma once

#include <cstdint>

namespace StarfieldHT {

inline constexpr const char* VERSION = "0.0.0";

inline constexpr const char* GAME_EXE = "Starfield.exe";

// Default UDP port for OpenTrack
inline constexpr uint16_t DEFAULT_UDP_PORT = 4242;

// Shared math constants
inline constexpr float DEG_TO_RAD = 0.0174533f;
inline constexpr float RAD_TO_DEG = 57.29578f;

// Default hotkey virtual key codes
inline constexpr int DEFAULT_TOGGLE_KEY = 0x23;           // VK_END - Enable/disable tracking
inline constexpr int DEFAULT_POSITION_TOGGLE_KEY = 0x21;  // VK_PRIOR (Page Up) - Cycle DOF mode
inline constexpr int DEFAULT_YAW_MODE_KEY = 0x22;         // VK_NEXT (Page Down) - Toggle world/local yaw
inline constexpr int DEFAULT_ADS_MODE_KEY = 0x2D;         // VK_INSERT - Cycle what the sights do

} // namespace StarfieldHT
