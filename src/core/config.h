#pragma once

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/head_tracking_config.h>
#include <cameraunlock/config/legacy_import.h>

#include <string>

namespace StarfieldHT {

constexpr const char* kConfigFileName = "HeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Starfield";

// Core's config, at core's defaults, which are the values every published build shipped.
struct Config : cameraunlock::HeadTrackingConfig {};

// The rows of HeadTracking.ini. Only the tracking mode pair and WorldSpaceYaw are Writable:
// the mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// The file as the builds before the canonical format read it (legacy_config/), mapped into
// Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for the config file at `path`.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& path);

} // namespace StarfieldHT
