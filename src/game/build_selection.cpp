#include "build_selection.h"
#include "discovery/native_contracts.h"

namespace StarfieldHT {
namespace {
using Field = uintptr_t BuildProfile::*;
const std::pair<const char*, Field> requiredFields[] = {
    {"screenProjectRva", &BuildProfile::screenProjectRva}, {"hudUpdateRva", &BuildProfile::hudUpdateRva}, {"weaponPassRva", &BuildProfile::weaponPassRva},
    {"cameraSubmitRva", &BuildProfile::cameraSubmitRva}, {"cameraRegistryRva", &BuildProfile::cameraRegistryRva}, {"gfxReleaseValueRva", &BuildProfile::gfxReleaseValueRva},
    {"playerSingletonRva", &BuildProfile::playerSingletonRva}, {"baseFovSettingRva", &BuildProfile::baseFovSettingRva}, {"shipAimRva", &BuildProfile::shipAimRva},
    {"shipPilotRva", &BuildProfile::shipPilotRva}, {"shipCameraConvertRva", &BuildProfile::shipCameraConvertRva}, {"relativeAimPointRva", &BuildProfile::relativeAimPointRva},
    {"playerAimPointOffset", &BuildProfile::playerAimPointOffset}, {"activeCameraRva", &BuildProfile::activeCameraRva}, {"shipLockCameraReturnRva", &BuildProfile::shipLockCameraReturnRva},
    {"selectionCameraManagerRva", &BuildProfile::selectionCameraManagerRva}, {"pickShipTargetRva", &BuildProfile::pickShipTargetRva}, {"scoreShipTargetRva", &BuildProfile::scoreShipTargetRva},
    {"scoreSpaceTargetRva", &BuildProfile::scoreSpaceTargetRva}, {"shipLockAngleRva", &BuildProfile::shipLockAngleRva}, {"shipLockUpdateRva", &BuildProfile::shipLockUpdateRva},
    {"playerCameraBoneOffset", &BuildProfile::playerCameraBoneOffset}, {"playerHitEventRva", &BuildProfile::playerHitEventRva}, {"hudHitEventRva", &BuildProfile::hudHitEventRva},
    {"worldOriginIndexRva", &BuildProfile::worldOriginIndexRva}
};
}

DiscoveredBuild DiscoverBuild(const discovery::Image& image) {
    DiscoveredBuild result;
    result.contracts = discovery::ResolveContracts(image, discovery::NativeContracts());
    for (const char* state : {".?AVFirstPersonState@@", ".?AVThirdPersonState@@", ".?AVBleedoutCameraState@@",
                              ".?AVFlightCameraState@@", ".?AVFurnitureCameraState@@", ".?AVShipActionCameraState@@"}) {
        const auto tables = discovery::FindVtables(image, state);
        for (const auto& table : tables) {
            if (table.objectOffset == 0) result.contracts.classes.emplace(state, table.rva);
        }
        discovery::Require(result.contracts.classes.count(state) == 1, std::string("Missing camera-state owner: ") + state);
    }
    const auto& c = result.contracts;
    auto& p = result.profile;
    p.name = "validated-native-contract";
    p.screenProjectRva = c.methods.at("Projection");
    p.hudUpdateRva = c.methods.at("HudUpdate");
    p.weaponPassRva = c.methods.at("RenderPass");
    p.cameraSubmitRva = c.methods.at("CameraSubmit");
    p.cameraRegistryRva = c.data.at("CameraRegistry");
    p.gfxReleaseValueRva = c.methods.at("ReleaseValue");
    p.playerSingletonRva = c.data.at("PlayerSingleton");
    p.baseFovSettingRva = c.data.at("BaseFovSetting");
    p.shipAimRva = c.methods.at("ShipAim");
    p.shipPilotRva = c.methods.at("ShipPilot");
    p.shipCameraConvertRva = c.methods.at("ShipConvert");
    p.relativeAimPointRva = c.methods.at("RelativePoint");
    p.playerAimPointOffset = c.members.at("PlayerAimPoint");
    p.activeCameraRva = c.methods.at("ActiveCamera");
    p.shipLockCameraReturnRva = c.returns.at("LockAngle/24/ActiveCamera");
    p.selectionCameraManagerRva = c.data.at("SelectionManager");
    p.pickShipTargetRva = c.methods.at("PickTarget");
    p.scoreShipTargetRva = c.methods.at("ScoreShip");
    p.scoreSpaceTargetRva = c.methods.at("ScoreSpace");
    p.shipLockAngleRva = c.methods.at("LockAngle");
    p.shipLockUpdateRva = c.methods.at("LockUpdate");
    p.playerCameraBoneOffset = c.members.at("PlayerCameraBone");
    p.playerHitEventRva = c.methods.at("PlayerHit");
    p.hudHitEventRva = c.methods.at("HudHit");
    p.worldOriginIndexRva = c.data.at("WorldOrigin");
    return result;
}

BuildSelection SelectBuild(cameraunlock::memory::PeFingerprint fingerprint,
                           const std::function<DiscoveredBuild()>& discover) {
    BuildSelection selected;
    const BuildProfile* known = nullptr;
    for (const auto* profile : {&kSteamProfile_20251129, &kGdkProfile_20251129}) {
        if (profile->fingerprint.Matches(fingerprint)) known = profile;
    }
    DiscoveredBuild candidate;
    try {
        candidate = discover();
        for (const auto& [name, field] : requiredFields) {
            discovery::Require(candidate.profile.*field != 0, std::string("Discovery returned no value for ") + name);
        }
    } catch (const std::exception& error) {
        selected.exact = known;
        selected.diagnostic = std::string("Native discovery rejected: ") + error.what();
        return selected;
    }
    if (known) {
        for (const auto& [name, field] : requiredFields) {
            if (candidate.profile.*field != known->*field) {
                selected.diagnostic = std::string("Native discovery disagrees with exact historical profile for ") + name + "; all hooks remain disabled";
                return selected;
            }
        }
    }
    candidate.profile.fingerprint = fingerprint;
    selected.discovered = std::move(candidate);
    selected.diagnostic = known ? "Native discovery validated and agrees with the exact historical profile"
                                : "Unlisted build accepted by complete native discovery";
    return selected;
}
}
