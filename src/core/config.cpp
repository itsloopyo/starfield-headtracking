#include "pch.h"
#include "config.h"
#include "logger.h"
#include "legacy_config/legacy_config.h"

namespace StarfieldHT {

// Inline member initializers on the Config struct are the single source of truth
// for defaults. SetDefaults() resets the whole struct to its freshly-constructed state.
void Config::SetDefaults() {
    *this = Config{};
}

Config MapLegacyConfig(const legacy::Config& read) {
    Config c;
    c.udpPort = read.udpPort;
    c.yawMultiplier = read.yawMultiplier;
    c.pitchMultiplier = read.pitchMultiplier;
    c.rollMultiplier = read.rollMultiplier;
    c.localSmoothing = read.localSmoothing;
    c.remoteSmoothing = read.remoteSmoothing;
    c.toggleKey = read.toggleKey;
    c.positionToggleKey = read.positionToggleKey;
    c.yawModeKey = read.yawModeKey;
    c.positionSensitivityX = read.positionSensitivityX;
    c.positionSensitivityY = read.positionSensitivityY;
    c.positionSensitivityZ = read.positionSensitivityZ;
    c.positionLimitX = read.positionLimitX;
    c.positionLimitY = read.positionLimitY;
    c.positionLimitZ = read.positionLimitZ;
    c.positionLimitZBack = read.positionLimitZBack;
    c.positionEnabled = read.positionEnabled;
    c.autoEnable = read.autoEnable;
    c.worldSpaceYaw = read.worldSpaceYaw;
    c.showCrosshair = read.showCrosshair;
    c.shipAimUIFollowsHead = read.shipAimUIFollowsHead;
    return c;
}

bool Config::Save(const char* path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::Instance().Error("Failed to save config to %s", path);
        return false;
    }

    file << "; Starfield Head Tracking Configuration\n";
    file << "; Delete this file to reset to defaults\n\n";

    file << "[Network]\n";
    file << "; UDP port for OpenTrack data (default: 4242)\n";
    file << "UDPPort=" << udpPort << "\n\n";

    file << "[Sensitivity]\n";
    file << "; Rotation sensitivity multipliers (1.0 = 1:1)\n";
    file << "YawMultiplier=" << yawMultiplier << "\n";
    file << "PitchMultiplier=" << pitchMultiplier << "\n";
    file << "RollMultiplier=" << rollMultiplier << "\n";
    file << "; Smoothing, applied to both rotation and position. The value is picked\n";
    file << "; per connection from the packet source address.\n";
    file << "; LocalSmoothing: tracker running on this machine (loopback).\n";
    file << "; RemoteSmoothing: tracker on a remote network device (phone on WiFi).\n";
    file << "; 0.0 = no smoothing, 1.0 = heavy. Raise for a noisier tracker - it\n";
    file << "; costs perceived latency.\n";
    file << "LocalSmoothing=" << localSmoothing << "\n";
    file << "RemoteSmoothing=" << remoteSmoothing << "\n\n";

    file << "[Position]\n";
    file << "; Position tracking sensitivity (0.0-5.0). Leave at 1.0 and shape the pose in\n";
    file << "; your tracker instead, so one profile behaves the same in every game.\n";
    file << "SensitivityX=" << positionSensitivityX << "\n";
    file << "SensitivityY=" << positionSensitivityY << "\n";
    file << "SensitivityZ=" << positionSensitivityZ << "\n";
    file << "; Position limits in meters (how far the camera can move)\n";
    file << "LimitX=" << positionLimitX << "\n";
    file << "LimitY=" << positionLimitY << "\n";
    file << "LimitZ=" << positionLimitZ << "\n";
    file << "; Backward lean limit (prevents camera clipping through player model)\n";
    file << "LimitZBack=" << positionLimitZBack << "\n";
    file << "; Enable/disable position tracking (6DOF)\n";
    file << "Enabled=" << (positionEnabled ? "true" : "false") << "\n\n";

    file << "[Hotkeys]\n";
    file << "; Virtual key codes (hex)\n";
    file << std::hex;
    file << "ToggleKey=0x" << toggleKey << "    ; End - Enable/disable\n";
    file << "PositionToggleKey=0x" << positionToggleKey << " ; Page Up - Cycle tracking mode\n";
    file << "YawModeKey=0x" << yawModeKey << "        ; Page Down - Toggle world/local yaw\n\n";
    // std::hex is sticky, so anything numeric added after this section would
    // otherwise be written in hex without the 0x that says so.
    file << std::dec;

    file << "[General]\n";
    file << "; Auto-enable tracking on game start\n";
    file << "AutoEnable=" << (autoEnable ? "true" : "false") << "\n";
    file << "; Horizon lock: true (default) turns head yaw about the world's up axis and\n";
    file << "; moves a lean along the ground, whatever the camera is pitched or rolled to.\n";
    file << "; false uses the camera's own axes for both.\n";
    file << "WorldSpaceYaw=" << (worldSpaceYaw ? "true" : "false") << "\n\n";

    file << "[Crosshair]\n";
    file << "; Reposition the game's native crosshair to follow your aim once\n";
    file << "; head tracking moves the view. Set false to leave it at centre.\n";
    file << "Show=" << (showCrosshair ? "true" : "false") << "\n";
    file << "\n[Ship]\n";
    file << "; false anchors the aim circle to the forward view. true keeps it head-fixed.\n";
    file << "; Restart the game after changing this setting.\n";
    file << "AimUIFollowsHead=" << (shipAimUIFollowsHead ? "true" : "false") << "\n";

    file.close();
    Logger::Instance().Info("Config saved to %s", path);
    return true;
}

} // namespace StarfieldHT
