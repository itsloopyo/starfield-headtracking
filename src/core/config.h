#pragma once

#include <cstdint>

#include "core/constants.h"

#include <cameraunlock/data/position_settings.h>
#include <cameraunlock/math/smoothing_utils.h>

namespace StarfieldHT {

struct Config {
    // Network settings
    uint16_t udpPort = DEFAULT_UDP_PORT;

    // Sensitivity multipliers
    float yawMultiplier = 1.0f;
    float pitchMultiplier = 1.0f;
    float rollMultiplier = 1.0f;

    // Smoothing. The value used is picked per connection from the packet source
    // address: a tracker on this machine (loopback) uses localSmoothing, a
    // remote network device uses remoteSmoothing. Both cover rotation and
    // position. 0.0 = none, 1.0 = heavy.
    float localSmoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // Hotkeys (Virtual Key codes)
    int toggleKey = DEFAULT_TOGGLE_KEY;
    int positionToggleKey = DEFAULT_POSITION_TOGGLE_KEY;
    int yawModeKey = DEFAULT_YAW_MODE_KEY;

    // Position settings (6DOF). No per-axis inversion: which way a tracker
    // calls positive is the tracker's to fix, once, in its own profile, and a
    // per-game knob here would be applied ahead of the asymmetric
    // [-LimitZ, +LimitZBack] clamp and quietly swap the two lean budgets.
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float positionLimitX = cameraunlock::PositionSettings{}.limit_x;
    float positionLimitY = cameraunlock::PositionSettings{}.limit_y;
    float positionLimitZ = cameraunlock::PositionSettings{}.limit_z;
    float positionLimitZBack = cameraunlock::PositionSettings{}.limit_z_back;
    bool positionEnabled = true;

    // General settings
    bool autoEnable = true;
    bool worldSpaceYaw = true;

    // showCrosshair = false leaves the game's own centre reticle where the game
    // put it, instead of moving it onto the aim.
    bool showCrosshair = true;
    bool shipAimUIFollowsHead = false;

    // Load/Save
    bool Load(const char* path);
    bool Save(const char* path) const;
    void SetDefaults();
    void Validate();

private:
    static int ConfigHandler(void* user, const char* section, const char* name, const char* value);
};

// The one mapping from the user's file to the processor's settings, so a test
// exercises what the mod runs rather than a second copy of it. Assigned by name
// rather than through the positional constructor: the argument list is long
// enough that a field added or removed upstream would silently rebind the
// limits to the wrong slots.
inline cameraunlock::PositionSettings ToPositionSettings(const Config& config) {
    cameraunlock::PositionSettings s;
    s.sensitivity_x = config.positionSensitivityX;
    s.sensitivity_y = config.positionSensitivityY;
    s.sensitivity_z = config.positionSensitivityZ;
    s.limit_x       = config.positionLimitX;
    // The clamp is [-limit_y_down, +limit_y] and limit_y_down carries its own
    // default, so mirror the one configured vertical limit the way
    // PositionSettings::Symmetric does. Left unset, raising LimitY widened the
    // upward budget only and downward travel stayed pinned at 0.20m.
    s.limit_y       = config.positionLimitY;
    s.limit_y_down  = config.positionLimitY;
    s.limit_z       = config.positionLimitZ;
    s.limit_z_back  = config.positionLimitZBack;
    return s;
}

} // namespace StarfieldHT
