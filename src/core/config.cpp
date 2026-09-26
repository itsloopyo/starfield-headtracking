#include "pch.h"
#include "config.h"

#include "legacy_config/legacy_config.h"

#include <cameraunlock/config/head_tracking_config_table.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include <utility>
#include <vector>

namespace StarfieldHT {

namespace {

namespace cfg = cameraunlock::config;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// A legacy hotkey code and the Ctrl+Shift chord the builds always registered beside it, as one
// key list.
std::string KeyList(int vk, char letter, const char* key, std::vector<cfg::DroppedValue>& dropped) {
    const std::string code = cfg::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    const std::string chord =
        cameraunlock::input::FormatKeyBindings({KeyBinding{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
    return code.empty() ? chord : code + ", " + chord;
}

cfg::ImportResult Import(const cfg::LegacyInput& input, Config& out) {
    legacy::Config c;
    const legacy::ReadStatus status = legacy::Read(input.ansi_path.c_str(), c);
    if (status == legacy::ReadStatus::OpenFailed) {
        return cfg::ImportResult::Refused(
            "the file could not be opened, so the mod runs on its default settings this session, "
            "as the last version did");
    }

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> shaping;

    out.udp_port = c.udpPort;
    out.enable_on_startup = c.autoEnable;
    out.world_space_yaw = c.worldSpaceYaw;

    // [Position] Enabled chose only the mode the session started in: the cycle key reached
    // every mode either way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.positionEnabled ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    // The frozen reader held both to [0, 1] and every limit to [0.01, 0.5].
    out.local_smoothing = c.localSmoothing;
    out.position.local_smoothing = c.localSmoothing;
    out.remote_smoothing = c.remoteSmoothing;
    out.position.remote_smoothing = c.remoteSmoothing;

    // LimitY bounded both directions, so it becomes both explicit values.
    out.position.limit_x = c.positionLimitX;
    out.position.limit_y = c.positionLimitY;
    out.position.limit_y_down = c.positionLimitY;
    out.position.limit_z = c.positionLimitZ;
    out.position.limit_z_back = c.positionLimitZBack;

    // Every sensitivity shipped at 1.0, identity, so nothing moves into the axis conversion. A
    // value the player changed is dropped.
    const auto shape = [&](float value, float shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, shaping, dropped);
    };
    shape(c.yawMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "YawMultiplier");
    shape(c.pitchMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "PitchMultiplier");
    shape(c.rollMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "RollMultiplier");
    shape(c.positionSensitivityX, legacy::kDefaultPositionSensitivity, "Position", "SensitivityX");
    shape(c.positionSensitivityY, legacy::kDefaultPositionSensitivity, "Position", "SensitivityY");
    shape(c.positionSensitivityZ, legacy::kDefaultPositionSensitivity, "Position", "SensitivityZ");

    // The game's crosshair, and the ship's aim circle, now always follow the aim. A file that
    // switched either off loses that switch.
    if (!c.showCrosshair) dropped.push_back({cfg::DropRule::Reticle, "Crosshair", "Show", "false"});
    if (c.shipAimUIFollowsHead) dropped.push_back({cfg::DropRule::Reticle, "Ship", "AimUIFollowsHead", "true"});

    out.toggle_key_name = KeyList(c.toggleKey, 'Y', "ToggleKey", dropped);
    out.cycle_tracking_mode_key_name = KeyList(c.positionToggleKey, 'G', "PositionToggleKey", dropped);
    out.yaw_mode_key_name = KeyList(c.yawModeKey, 'H', "YawModeKey", dropped);

    return status == legacy::ReadStatus::Absent ? cfg::ImportResult::Absent(std::move(dropped), std::move(shaping))
                                                : cfg::ImportResult::Imported(std::move(dropped), std::move(shaping));
}

} // namespace

cfg::ConfigTable<Config> MakeConfigTable() {
    using C = cfg::schema::Concept;
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::PositionLimitX, C::PositionLimitY, C::PositionLimitYDown,
         C::PositionLimitZ, C::PositionLimitZBack, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey,
         C::LightFollowsHead, C::LightMultiplier});
    table.Select(C::WorldSpaceYaw).Writable()
        .Comment("true: yaw turns around the world's up axis and a lean moves along the ground.\n"
                 "false: both follow the camera's own axes.")
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable();
    return table;
}

cfg::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cfg::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = folder + kConfigFileName;
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.legacy_path = folder + kLegacyFileName;
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

} // namespace StarfieldHT
