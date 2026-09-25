#include "pch.h"
#include "legacy_config.h"
#include "core/logger.h"

extern "C" {
#include "ini.h"
}

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace StarfieldHT::legacy {

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

void Validate(Config& c) {
    c.yawMultiplier = std::clamp(c.yawMultiplier, 0.1f, 3.0f);
    c.pitchMultiplier = std::clamp(c.pitchMultiplier, 0.1f, 3.0f);
    c.rollMultiplier = std::clamp(c.rollMultiplier, 0.1f, 3.0f);

    c.localSmoothing = std::clamp(c.localSmoothing, 0.0f, 1.0f);
    c.remoteSmoothing = std::clamp(c.remoteSmoothing, 0.0f, 1.0f);

    c.positionSensitivityX = std::clamp(c.positionSensitivityX, 0.0f, 5.0f);
    c.positionSensitivityY = std::clamp(c.positionSensitivityY, 0.0f, 5.0f);
    c.positionSensitivityZ = std::clamp(c.positionSensitivityZ, 0.0f, 5.0f);

    c.positionLimitX = std::clamp(c.positionLimitX, 0.01f, 0.5f);
    c.positionLimitY = std::clamp(c.positionLimitY, 0.01f, 0.5f);
    c.positionLimitZ = std::clamp(c.positionLimitZ, 0.01f, 0.5f);
    c.positionLimitZBack = std::clamp(c.positionLimitZBack, 0.01f, 0.5f);

    c.toggleKey         = ValidVirtualKey(c.toggleKey, kDefaultToggleKey, "ToggleKey");
    c.positionToggleKey = ValidVirtualKey(c.positionToggleKey, kDefaultPositionToggleKey, "PositionToggleKey");
    c.yawModeKey        = ValidVirtualKey(c.yawModeKey, kDefaultYawModeKey, "YawModeKey");
}

void WarnUnparsed(const char* section, const char* name, const char* value) {
    Logger::Instance().Warning(
        "Config value [%s] %s=%s could not be read - keeping the previous value",
        section, name, value);
}

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

int ConfigHandler(void* user, const char* section, const char* name, const char* value) {
    Config* config = static_cast<Config*>(user);

#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)
#define NUMBER(current) ParseFloat(value, section, name, current)
#define WHOLE(current)  ParseInt(value, section, name, current)
#define FLAG(current)   ParseBool(value, section, name, current)

    if (MATCH("Network", "UDPPort")) {
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

    else if (MATCH("Sensitivity", "RotationSmoothing")) { WarnRetiredSmoothingKey("Sensitivity", "RotationSmoothing"); }
    else if (MATCH("Position", "Smoothing"))            { WarnRetiredSmoothingKey("Position", "Smoothing"); }

    else if (MATCH("Hotkeys", "ToggleKey"))         { config->toggleKey         = WHOLE(config->toggleKey); }
    else if (MATCH("Hotkeys", "PositionToggleKey")) { config->positionToggleKey = WHOLE(config->positionToggleKey); }
    else if (MATCH("Hotkeys", "YawModeKey"))        { config->yawModeKey        = WHOLE(config->yawModeKey); }

    else if (MATCH("Position", "SensitivityX")) { config->positionSensitivityX = NUMBER(config->positionSensitivityX); }
    else if (MATCH("Position", "SensitivityY")) { config->positionSensitivityY = NUMBER(config->positionSensitivityY); }
    else if (MATCH("Position", "SensitivityZ")) { config->positionSensitivityZ = NUMBER(config->positionSensitivityZ); }
    else if (MATCH("Position", "LimitX"))       { config->positionLimitX       = NUMBER(config->positionLimitX); }
    else if (MATCH("Position", "LimitY"))       { config->positionLimitY       = NUMBER(config->positionLimitY); }
    else if (MATCH("Position", "LimitZ"))       { config->positionLimitZ       = NUMBER(config->positionLimitZ); }
    else if (MATCH("Position", "LimitZBack"))   { config->positionLimitZBack   = NUMBER(config->positionLimitZBack); }
    else if (MATCH("Position", "Enabled"))      { config->positionEnabled      = FLAG(config->positionEnabled); }

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

} // namespace

ReadStatus Read(const char* iniPath, Config& cfg) {
    cfg = Config{};

    const int result = ini_parse(iniPath, ConfigHandler, &cfg);
    if (result < 0) {
        Logger::Instance().Warning("Could not load config from %s, using defaults", iniPath);
        cfg = Config{};
        if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) return ReadStatus::Absent;
        return ReadStatus::OpenFailed;
    }
    if (result > 0) {
        Logger::Instance().Warning("Config parse error on line %d", result);
    }

    Validate(cfg);
    Logger::Instance().Info("Config loaded from %s", iniPath);
    return ReadStatus::Read;
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"Network", "UDPPort"},
        {"Sensitivity", "YawMultiplier"},
        {"Sensitivity", "PitchMultiplier"},
        {"Sensitivity", "RollMultiplier"},
        {"Sensitivity", "LocalSmoothing"},
        {"Sensitivity", "RemoteSmoothing"},
        {"Hotkeys", "ToggleKey"},
        {"Hotkeys", "PositionToggleKey"},
        {"Hotkeys", "YawModeKey"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "Enabled"},
        {"General", "AutoEnable"},
        {"General", "WorldSpaceYaw"},
        {"Crosshair", "Show"},
        {"Ship", "AimUIFollowsHead"},
    };
}

} // namespace StarfieldHT::legacy
