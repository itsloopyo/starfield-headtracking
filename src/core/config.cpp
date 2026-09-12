#include "pch.h"
#include "config.h"
#include "logger.h"

extern "C" {
#include "ini.h"
}

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace StarfieldHT {

namespace {

// GetAsyncKeyState defines virtual key codes 0x01 to 0xFE and nothing else, so
// a code outside that range never fires and the user loses the binding with no
// diagnostic. strtol's base-0 parse reaches here with anything the file holds:
// a negative number, a decimal typed where hex was meant, or a scancode.
int ValidVirtualKey(int vkCode, int fallback, const char* name) {
    if (vkCode >= 0x01 && vkCode <= 0xFE) return vkCode;
    Logger::Instance().Warning(
        "Hotkey %s=0x%X is not a virtual key code (valid range 0x01-0xFE) - using 0x%X",
        name, static_cast<unsigned>(vkCode), static_cast<unsigned>(fallback));
    return fallback;
}

} // namespace

// Inline member initializers on the Config struct are the single source of truth
// for defaults. SetDefaults() resets the whole struct to its freshly-constructed state.
void Config::SetDefaults() {
    *this = Config{};
}

// Every range here is the one the shared configuration table specifies, so a
// value carries the same meaning in this mod as in every other. The position
// limits matter most: they are metres of eye travel and nothing sweeps the
// level geometry yet, so a ceiling of 0.5 is what keeps a lean inside the room
// the player is standing in.
void Config::Validate() {
    yawMultiplier = std::clamp(yawMultiplier, 0.1f, 3.0f);
    pitchMultiplier = std::clamp(pitchMultiplier, 0.1f, 3.0f);
    rollMultiplier = std::clamp(rollMultiplier, 0.1f, 3.0f);

    localSmoothing = std::clamp(localSmoothing, 0.0f, 1.0f);
    remoteSmoothing = std::clamp(remoteSmoothing, 0.0f, 1.0f);

    positionSensitivityX = std::clamp(positionSensitivityX, 0.0f, 5.0f);
    positionSensitivityY = std::clamp(positionSensitivityY, 0.0f, 5.0f);
    positionSensitivityZ = std::clamp(positionSensitivityZ, 0.0f, 5.0f);

    positionLimitX = std::clamp(positionLimitX, 0.01f, 0.5f);
    positionLimitY = std::clamp(positionLimitY, 0.01f, 0.5f);
    positionLimitZ = std::clamp(positionLimitZ, 0.01f, 0.5f);
    positionLimitZBack = std::clamp(positionLimitZBack, 0.01f, 0.5f);

    toggleKey         = ValidVirtualKey(toggleKey, DEFAULT_TOGGLE_KEY, "ToggleKey");
    positionToggleKey = ValidVirtualKey(positionToggleKey, DEFAULT_POSITION_TOGGLE_KEY, "PositionToggleKey");
    yawModeKey        = ValidVirtualKey(yawModeKey, DEFAULT_YAW_MODE_KEY, "YawModeKey");
    adsModeKey        = ValidVirtualKey(adsModeKey, DEFAULT_ADS_MODE_KEY, "AdsModeKey");
}

namespace {

void WarnUnparsed(const char* section, const char* name, const char* value) {
    Logger::Instance().Warning(
        "Config value [%s] %s=%s could not be read - keeping the previous value",
        section, name, value);
}

// The INI is the one place a hand-typed string becomes a number the rest of the
// mod trusts, so a value that does not parse is named rather than folded into a
// default. atoi/atof did the folding silently: "yes" and "on" both read as
// false, and "0,4" - what a comma-decimal keyboard produces - read as 0 and then
// clamped to a one-centimetre lean that looks exactly like broken tracking.
bool ParseBool(const char* value, const char* section, const char* name, bool current) {
    for (const char* yes : {"true", "yes", "on", "1"}) {
        if (_stricmp(value, yes) == 0) return true;
    }
    for (const char* no : {"false", "no", "off", "0"}) {
        if (_stricmp(value, no) == 0) return false;
    }
    WarnUnparsed(section, name, value);
    return current;
}

float ParseFloat(const char* value, const char* section, const char* name, float current) {
    char* end = nullptr;
    const double parsed = strtod(value, &end);
    if (end == value || !std::isfinite(parsed)) {
        WarnUnparsed(section, name, value);
        return current;
    }
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0') {
        WarnUnparsed(section, name, value);
        return current;
    }
    return static_cast<float>(parsed);
}

int ParseInt(const char* value, const char* section, const char* name, int current) {
    char* end = nullptr;
    const long parsed = strtol(value, &end, 0);
    if (end == value) {
        WarnUnparsed(section, name, value);
        return current;
    }
    return static_cast<int>(parsed);
}

// Retired keys are only reached when the user's file actually carries them:
// inih only invokes the handler for keys that exist. Both warnings are one-shot
// per process rather than per load, because config is reloadable and repeating
// them buries them. One line however many of the three axes a file carries.
void WarnRetiredPositionInvert(const char* key) {
    static bool warned = false;
    if (warned) return;
    warned = true;
    Logger::Instance().Warning(
        "Config key [Position] %s has been retired and is IGNORED. Which direction a tracker "
        "calls positive is the tracker's to fix, in its own profile, so it behaves the same in "
        "every game - OpenTrack has per-axis inversion under Options. Inverting here also ran "
        "ahead of the forward/back lean clamp and quietly swapped the 0.40m and 0.10m budgets.",
        key);
}

// The old smoothing value is deliberately NOT migrated into the two new keys.
// Both retired single-value keys carried a hidden 0.15 floor, so the number in an
// existing config does not mean what it used to: copying it across would hand a
// local user smoothing they never chose under the new semantics, and copying it
// into only one of the two keys would be a guess about which connection they were
// on.
void WarnRetiredSmoothingKey(const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    warned = true;
    Logger::Instance().Warning(
        "Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

} // namespace

int Config::ConfigHandler(void* user, const char* section, const char* name, const char* value) {
    Config* config = static_cast<Config*>(user);

#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)
#define NUMBER(current) ParseFloat(value, section, name, current)
#define WHOLE(current)  ParseInt(value, section, name, current)
#define FLAG(current)   ParseBool(value, section, name, current)

    if (MATCH("Network", "UDPPort")) {
        // atoi truncates silently: "70000" would wrap to 4464, "-1" to 65535.
        // Parse wide, range-check, and keep the default on bad input.
        long port = strtol(value, nullptr, 10);
        if (port >= 1 && port <= 65535) {
            config->udpPort = static_cast<uint16_t>(port);
        } else {
            Logger::Instance().Warning("UDPPort %ld out of range [1-65535], keeping %d",
                                       port, config->udpPort);
        }
    }
    else if (MATCH("Sensitivity", "YawMultiplier"))   { config->yawMultiplier   = NUMBER(config->yawMultiplier); }
    else if (MATCH("Sensitivity", "PitchMultiplier")) { config->pitchMultiplier = NUMBER(config->pitchMultiplier); }
    else if (MATCH("Sensitivity", "RollMultiplier"))  { config->rollMultiplier  = NUMBER(config->rollMultiplier); }
    else if (MATCH("Sensitivity", "LocalSmoothing"))  { config->localSmoothing  = NUMBER(config->localSmoothing); }
    else if (MATCH("Sensitivity", "RemoteSmoothing")) { config->remoteSmoothing = NUMBER(config->remoteSmoothing); }

    // Retired keys, both replaced by LocalSmoothing/RemoteSmoothing above. Matched only
    // so the user gets told they are dead instead of the values silently vanishing. The
    // helper's one-shot flag is shared, so an INI carrying both still logs one line.
    else if (MATCH("Sensitivity", "RotationSmoothing")) { WarnRetiredSmoothingKey("Sensitivity", "RotationSmoothing"); }
    else if (MATCH("Position", "Smoothing"))            { WarnRetiredSmoothingKey("Position", "Smoothing"); }

    else if (MATCH("Hotkeys", "ToggleKey"))         { config->toggleKey         = WHOLE(config->toggleKey); }
    else if (MATCH("Hotkeys", "PositionToggleKey")) { config->positionToggleKey = WHOLE(config->positionToggleKey); }
    else if (MATCH("Hotkeys", "YawModeKey"))        { config->yawModeKey        = WHOLE(config->yawModeKey); }
    else if (MATCH("Hotkeys", "AdsModeKey"))        { config->adsModeKey        = WHOLE(config->adsModeKey); }

    else if (MATCH("Position", "SensitivityX")) { config->positionSensitivityX = NUMBER(config->positionSensitivityX); }
    else if (MATCH("Position", "SensitivityY")) { config->positionSensitivityY = NUMBER(config->positionSensitivityY); }
    else if (MATCH("Position", "SensitivityZ")) { config->positionSensitivityZ = NUMBER(config->positionSensitivityZ); }
    else if (MATCH("Position", "LimitX"))       { config->positionLimitX       = NUMBER(config->positionLimitX); }
    else if (MATCH("Position", "LimitY"))       { config->positionLimitY       = NUMBER(config->positionLimitY); }
    else if (MATCH("Position", "LimitZ"))       { config->positionLimitZ       = NUMBER(config->positionLimitZ); }
    else if (MATCH("Position", "LimitZBack"))   { config->positionLimitZBack   = NUMBER(config->positionLimitZBack); }
    else if (MATCH("Position", "Enabled"))      { config->positionEnabled      = FLAG(config->positionEnabled); }

    // Retired with the same reasoning as the smoothing keys below: which way a
    // tracker calls positive is fixed once in the tracker's own profile, and an
    // inversion here landed ahead of the asymmetric forward/back clamp and swapped
    // the two lean budgets.
    else if (MATCH("Position", "InvertX") || MATCH("Position", "InvertY")
             || MATCH("Position", "InvertZ")) {
        WarnRetiredPositionInvert(name);
    }

    else if (MATCH("General", "AutoEnable"))        { config->autoEnable        = FLAG(config->autoEnable); }
    else if (MATCH("General", "WorldSpaceYaw"))     { config->worldSpaceYaw     = FLAG(config->worldSpaceYaw); }

    else if (MATCH("Crosshair", "Show"))            { config->showCrosshair     = FLAG(config->showCrosshair); }
    else if (MATCH("Ship", "AimUIFollowsHead"))     { config->shipAimUIFollowsHead = FLAG(config->shipAimUIFollowsHead); }

#undef MATCH
#undef NUMBER
#undef WHOLE
#undef FLAG

    return 1;
}

bool Config::Load(const char* path) {
    SetDefaults();

    int result = ini_parse(path, ConfigHandler, this);
    if (result < 0) {
        Logger::Instance().Warning("Could not load config from %s, using defaults", path);
        return false;
    }
    if (result > 0) {
        Logger::Instance().Warning("Config parse error on line %d", result);
    }

    Validate();
    Logger::Instance().Info("Config loaded from %s", path);
    return true;
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
    file << "YawModeKey=0x" << yawModeKey << "        ; Page Down - Toggle world/local yaw\n";
    file << "AdsModeKey=0x" << adsModeKey << "        ; Insert - Cycle what the sights do\n\n";
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
