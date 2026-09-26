#pragma once

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/head_tracking_config.h>
#include <cameraunlock/config/legacy_import.h>

#include <string>

namespace StarfieldHT {

// The settings file, and the file every build before it read, which the owner imports while the
// settings file is absent and never writes.
constexpr const wchar_t* kConfigFileName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyFileName = L"HeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Starfield";

// Core's config, at core's defaults, which are the values every published build shipped.
struct Config : cameraunlock::HeadTrackingConfig {};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable:
// the mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// HeadTracking.ini as the builds before the canonical format read it (legacy_config/), mapped
// into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for CameraUnlock.ini in `folder`, a full path ending in a separator, with
// HeadTracking.ini beside it as the legacy file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder,
                                                                         cameraunlock::config::DefaultsFile defaults);

} // namespace StarfieldHT
