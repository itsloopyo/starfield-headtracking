#include "oracle_adapter.h"

#include "pch.h"
#include "core/config.h"

namespace oracle_api {

namespace {

Config Copy(const sf_oracle::Config& c) {
    Config out;
    out.udpPort = c.udpPort;
    out.yawMultiplier = c.yawMultiplier;
    out.pitchMultiplier = c.pitchMultiplier;
    out.rollMultiplier = c.rollMultiplier;
    out.localSmoothing = c.localSmoothing;
    out.remoteSmoothing = c.remoteSmoothing;
    out.toggleKey = c.toggleKey;
    out.positionToggleKey = c.positionToggleKey;
    out.yawModeKey = c.yawModeKey;
    out.adsModeKey = c.adsModeKey;
    out.positionSensitivityX = c.positionSensitivityX;
    out.positionSensitivityY = c.positionSensitivityY;
    out.positionSensitivityZ = c.positionSensitivityZ;
    out.positionLimitX = c.positionLimitX;
    out.positionLimitY = c.positionLimitY;
    out.positionLimitZ = c.positionLimitZ;
    out.positionLimitZBack = c.positionLimitZBack;
    out.positionEnabled = c.positionEnabled;
    out.autoEnable = c.autoEnable;
    out.worldSpaceYaw = c.worldSpaceYaw;
    out.showCrosshair = c.showCrosshair;
    out.shipAimUIFollowsHead = c.shipAimUIFollowsHead;
    return out;
}

}  // namespace

Config Defaults() {
    return Copy(sf_oracle::Config{});
}

// The published build's Mod::LoadConfig (src/core/mod.cpp at d276538), less its logging.
LoadStatus LoadOrCreate(const char* iniPath, Config& out) {
    sf_oracle::Config c;
    LoadStatus status = LoadStatus::Read;
    if (!c.Load(iniPath)) {
        c.SetDefaults();
        if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
            c.Save(iniPath);
            status = LoadStatus::Created;
        } else {
            status = LoadStatus::OpenFailed;
        }
    }
    out = Copy(c);
    return status;
}

}  // namespace oracle_api
