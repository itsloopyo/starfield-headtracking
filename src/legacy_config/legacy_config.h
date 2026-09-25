#pragma once

#include <cameraunlock/config/legacy_import.h>

#include <cstdint>
#include <vector>

// The HeadTracking.ini reader as src/core/config.cpp held it at 43f310d, the last commit before
// the canonical config format, frozen so an old file converts exactly as the builds before it
// read it. Never edited: a change here changes what a player's old file means.
//
// It differs from that commit's Config::Load in three ways only. It fills this frozen copy of
// that commit's Config rather than the runtime one. It writes nothing: where the build wrote a
// file of defaults over a missing one, this reports Absent and leaves the defaults, which is
// what reading that file gave. And it reports a file that exists and cannot be opened as
// OpenFailed, which the build told apart from a missing one in Mod::LoadConfig.
//
// The defaults are literals rather than the constants the runtime took them from (constants.h,
// and cameraunlock-core's PositionSettings and smoothing defaults, the same at ee8cc72,
// c480d8a and befb88e), so a later default cannot move what an old file without the key means.

namespace StarfieldHT::legacy {

constexpr uint16_t kDefaultUdpPort              = 4242;
constexpr float    kDefaultMultiplier           = 1.0f;
constexpr float    kDefaultLocalSmoothing       = static_cast<float>(0.0);
constexpr float    kDefaultRemoteSmoothing      = static_cast<float>(0.15);
constexpr int      kDefaultToggleKey            = 0x23;
constexpr int      kDefaultPositionToggleKey    = 0x21;
constexpr int      kDefaultYawModeKey           = 0x22;
constexpr float    kDefaultPositionSensitivity  = 1.0f;
constexpr float    kDefaultPositionLimitX       = 0.30f;
constexpr float    kDefaultPositionLimitY       = 0.20f;
constexpr float    kDefaultPositionLimitZ       = 0.40f;
constexpr float    kDefaultPositionLimitZBack   = 0.10f;
constexpr bool     kDefaultPositionEnabled      = true;
constexpr bool     kDefaultAutoEnable           = true;
constexpr bool     kDefaultWorldSpaceYaw        = true;
constexpr bool     kDefaultShowCrosshair        = true;
constexpr bool     kDefaultShipAimUIFollowsHead = false;

struct Config {
    uint16_t udpPort = kDefaultUdpPort;

    float yawMultiplier = kDefaultMultiplier;
    float pitchMultiplier = kDefaultMultiplier;
    float rollMultiplier = kDefaultMultiplier;

    float localSmoothing = kDefaultLocalSmoothing;
    float remoteSmoothing = kDefaultRemoteSmoothing;

    int toggleKey = kDefaultToggleKey;
    int positionToggleKey = kDefaultPositionToggleKey;
    int yawModeKey = kDefaultYawModeKey;

    float positionSensitivityX = kDefaultPositionSensitivity;
    float positionSensitivityY = kDefaultPositionSensitivity;
    float positionSensitivityZ = kDefaultPositionSensitivity;
    float positionLimitX = kDefaultPositionLimitX;
    float positionLimitY = kDefaultPositionLimitY;
    float positionLimitZ = kDefaultPositionLimitZ;
    float positionLimitZBack = kDefaultPositionLimitZBack;
    bool positionEnabled = kDefaultPositionEnabled;

    bool autoEnable = kDefaultAutoEnable;
    bool worldSpaceYaw = kDefaultWorldSpaceYaw;

    bool showCrosshair = kDefaultShowCrosshair;
    bool shipAimUIFollowsHead = kDefaultShipAimUIFollowsHead;
};

enum class ReadStatus {
    // The file was read into the Config and clamped as the build clamped it.
    Read,
    // There is no file at the path. The build ran on the defaults and wrote a file holding
    // them; the Config holds the defaults.
    Absent,
    // The file exists and could not be opened. The build ran on the defaults and left the
    // file alone; the Config holds the defaults.
    OpenFailed,
};

// Reads the file at the ANSI path through inih (extern/ini.c), as the build did, and clamps
// what it read as its Validate did. Writes nothing. Logs through the mod's Logger as the build
// did.
ReadStatus Read(const char* iniPath, Config& cfg);

// Every section and key Read takes a value from. The retired keys it only warns about
// ([Sensitivity] RotationSmoothing, [Position] Smoothing and InvertX/Y/Z) are not here: the
// build ignored their values.
std::vector<cameraunlock::config::LegacyKey> ReadKeys();

} // namespace StarfieldHT::legacy
