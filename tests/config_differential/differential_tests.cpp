// The config differential test. Every input is read three ways:
//
//   oracle     the published build's reader (oracle/), the dev pre-release at d276538, and
//              its startup code
//   import     the frozen reader in src/legacy_config/, and the startup code it ran under
//   migration  the config owner converting the file, then the canonical reader and table on
//              the result, and the startup code of this build
//
// Comparison 1, oracle against import, finds what a player updating from the published build
// sees change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Comparison 2, import against migration, is the proof for the migration: no difference but
// the approved ones, each of which the import must record as dropped. A sensitivity the player
// set away from its shipped 1.0 is dropped (pose_shaping), and so is a [Crosshair] Show=false
// or a [Ship] AimUIFollowsHead=true, since the game's crosshair and the ship's aim circle now
// always follow the aim (reticle). No default moved, so the no-file input has no difference
// either. The frozen reader clamps every number and hotkey code it reads into a range the
// canonical rows hold, so no input is deferred and no N1 or N2 applies.
//
// The distinct migrated files are written beside the executable under migrated\, for
// lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the HeadTracking.ini the dev pre-release shipped (its
// installer ZIP's plugins\HeadTracking.ini and its launcher seed are the same bytes, and the
// only version of the file committed up to it), the file that build writes at first launch
// when there is none (extracted once into inputs/), and core's corpus over the shipped file.
// v0.0.1 is a tag with no release; its reader, its shipped file and its core pin are the dev
// build's.

#include "core/config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = StarfieldHT::legacy;
namespace cfg = cameraunlock::config;
using StarfieldHT::Config;
using cameraunlock::TrackingMode;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;

int g_failures = 0;

void Fail(const std::string& input, const std::string& what) {
    if (g_failures < 50) std::printf("FAIL [%s]: %s\n", input.c_str(), what.c_str());
    ++g_failures;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

std::string Narrow(const std::wstring& path) {
    const int size = WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), 'x');
    WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size) - 1);
    return out;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + Narrow(path));
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + Narrow(path));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Every file in the folder, name and bytes, for "the import changed nothing".
std::map<std::wstring, std::string> Snapshot(const std::wstring& dir) {
    std::map<std::wstring, std::string> files;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot list the test folder");
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files[data.cFileName] = ReadBytes(dir + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return files;
}

void EmptyFolder(const std::wstring& dir) {
    for (const auto& [name, bytes] : Snapshot(dir)) {
        const std::wstring path = dir + L"\\" + name;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(path.c_str())) throw std::runtime_error("cannot empty the test folder");
    }
}

std::wstring MakeFolder(const std::wstring& parent, const wchar_t* name) {
    const std::wstring dir = parent + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("cannot create the test folder");
    }
    EmptyFolder(dir);
    return dir;
}

// ---------------------------------------------------------------------------
// Hotkeys: what each build puts on its poller
// ---------------------------------------------------------------------------

enum class Action { Toggle, CycleMode, YawMode, AdsMode };

const char* ActionName(Action a) {
    switch (a) {
        case Action::Toggle: return "toggle";
        case Action::CycleMode: return "cycle mode";
        case Action::YawMode: return "yaw mode";
        case Action::AdsMode: return "sights mode";
    }
    throw std::logic_error("action");
}

// One key the poller watches for an action, and the modifiers it fires with: 0 is NavGuarded
// (not while Ctrl and Shift are both held), kChord is ChordGuarded (while both are held).
struct Registration {
    Action action;
    int vk;
    unsigned modifiers;

    bool operator<(const Registration& o) const {
        return std::tie(action, vk, modifiers) < std::tie(o.action, o.vk, o.modifiers);
    }
    bool operator==(const Registration& o) const {
        return action == o.action && vk == o.vk && modifiers == o.modifiers;
    }
    bool operator!=(const Registration& o) const { return !(*this == o); }
};

using cameraunlock::input::KeyModifiers;
constexpr unsigned kNav = static_cast<unsigned>(KeyModifiers::kNone);
constexpr unsigned kChord = static_cast<unsigned>(KeyModifiers::kCtrl | KeyModifiers::kShift);

std::string Describe(const std::vector<Registration>& regs) {
    std::string out;
    for (const Registration& r : regs) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%s%s:%s0x%02X", out.empty() ? "" : " ", ActionName(r.action),
                      r.modifiers == kChord ? "Ctrl+Shift+" : "", static_cast<unsigned>(r.vk));
        out += buf;
    }
    return out;
}

// RegisterBindings at d276538 (src/hooks/input_hook.cpp), with the poller calls recorded. The
// chords were registered unconditionally.
std::vector<Registration> OracleHotkeys(const oracle_api::Config& c) {
    std::vector<Registration> regs = {
        {Action::Toggle, c.toggleKey, kNav},
        {Action::CycleMode, c.positionToggleKey, kNav},
        {Action::YawMode, c.yawModeKey, kNav},
        {Action::AdsMode, c.adsModeKey, kNav},
        {Action::Toggle, 'Y', kChord},
        {Action::CycleMode, 'G', kChord},
        {Action::YawMode, 'H', kChord},
        {Action::AdsMode, 'U', kChord},
    };
    std::sort(regs.begin(), regs.end());
    return regs;
}

// RegisterBindings as the build that carries the frozen reader runs it (src/hooks/input_hook.cpp
// at 43f310d), with the poller calls recorded.
std::vector<Registration> ImportHotkeys(const legacy::Config& c) {
    std::vector<Registration> regs = {
        {Action::Toggle, c.toggleKey, kNav},
        {Action::CycleMode, c.positionToggleKey, kNav},
        {Action::YawMode, c.yawModeKey, kNav},
        {Action::Toggle, 'Y', kChord},
        {Action::CycleMode, 'G', kChord},
        {Action::YawMode, 'H', kChord},
    };
    std::sort(regs.begin(), regs.end());
    return regs;
}

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// Every field the two Configs share, floats bit for bit. The oracle's adsModeKey has no
// counterpart; comparison 1 lists it. Both builds' Mod::Initialize takes the whole of its
// startup state from these fields, so equal fields are an equal start.
template <class A, class B>
std::vector<std::string> SharedFieldDifferences(const A& a, const B& b) {
    std::vector<std::string> out;
    const auto check = [&out](bool same, const char* name) {
        if (!same) out.push_back(name);
    };
#define SAME(f) check(a.f == b.f, #f)
#define SAME_BITS(f) check(Bits(a.f) == Bits(b.f), #f)
    SAME(udpPort);
    SAME_BITS(yawMultiplier);
    SAME_BITS(pitchMultiplier);
    SAME_BITS(rollMultiplier);
    SAME_BITS(localSmoothing);
    SAME_BITS(remoteSmoothing);
    SAME(toggleKey);
    SAME(positionToggleKey);
    SAME(yawModeKey);
    SAME_BITS(positionSensitivityX);
    SAME_BITS(positionSensitivityY);
    SAME_BITS(positionSensitivityZ);
    SAME_BITS(positionLimitX);
    SAME_BITS(positionLimitY);
    SAME_BITS(positionLimitZ);
    SAME_BITS(positionLimitZBack);
    SAME(positionEnabled);
    SAME(autoEnable);
    SAME(worldSpaceYaw);
    SAME(showCrosshair);
    SAME(shipAimUIFollowsHead);
#undef SAME
#undef SAME_BITS
    return out;
}

// ---------------------------------------------------------------------------
// Comparison 1: the published build against the frozen reader
// ---------------------------------------------------------------------------

// What a player updating from the published build sees change, and the commit that made each
// change. The changelog carries the same list.
struct ListedDifference {
    const char* id;
    const char* commit;
    const char* what;
    int seen = 0;
};

ListedDifference kComparisonOneDifferences[] = {
    {"sights-key", "be563fd",
     "[Hotkeys] AdsModeKey is no longer read, and neither its key (Insert unless the player "
     "changed it) nor Ctrl+Shift+U cycles what the sights do: head tracking carries on through "
     "the sights"},
};

ListedDifference& Listed(const char* id) {
    for (ListedDifference& d : kComparisonOneDifferences) {
        if (std::strcmp(d.id, id) == 0) return d;
    }
    throw std::logic_error(id);
}

struct OracleRun {
    oracle_api::LoadStatus status = oracle_api::LoadStatus::Read;
    oracle_api::Config cfg;
};

struct ImportRun {
    legacy::ReadStatus status = legacy::ReadStatus::Read;
    legacy::Config cfg;
};

bool SameStatus(oracle_api::LoadStatus o, legacy::ReadStatus i) {
    switch (o) {
        case oracle_api::LoadStatus::Read: return i == legacy::ReadStatus::Read;
        case oracle_api::LoadStatus::Created: return i == legacy::ReadStatus::Absent;
        case oracle_api::LoadStatus::OpenFailed: return i == legacy::ReadStatus::OpenFailed;
    }
    throw std::logic_error("status");
}

void CompareOracleWithImport(const std::string& name, const OracleRun& o, const ImportRun& i) {
    if (!SameStatus(o.status, i.status)) {
        Fail(name, "comparison 1: the published build and the import do not agree on whether the file was read");
        return;
    }

    for (const std::string& field : SharedFieldDifferences(o.cfg, i.cfg)) {
        Fail(name, "comparison 1: " + field + " differs from the published build with no listed reason");
    }

    std::vector<Registration> expected;
    bool sightsBound = false;
    for (const Registration& r : OracleHotkeys(o.cfg)) {
        if (r.action == Action::AdsMode) {
            sightsBound = true;
        } else {
            expected.push_back(r);
        }
    }
    if (sightsBound) ++Listed("sights-key").seen;
    const std::vector<Registration> actual = ImportHotkeys(i.cfg);
    if (expected != actual) {
        Fail(name, "comparison 1: hotkeys " + Describe(actual) +
                       ", the published build less the listed differences " + Describe(expected));
    }
}

// ---------------------------------------------------------------------------
// The keys the frozen reader reads, and the corpus descriptors
// ---------------------------------------------------------------------------

std::vector<MutationKey> CorpusKeys() {
    const auto boolean = [](const char* s, const char* k, const char* alternate) {
        return MutationKey{s, k, alternate, {}, false, {}};
    };
    const auto multiplier = [](const char* k) { return MutationKey{"Sensitivity", k, "0.5", {"0.05", "3.5"}, false, {}}; };
    const auto smooth = [](const char* k) { return MutationKey{"Sensitivity", k, "0.3", {"-0.5", "1.5"}, false, {}}; };
    const auto sens = [](const char* k) { return MutationKey{"Position", k, "0.5", {"-1", "6"}, false, {}}; };
    const auto limit = [](const char* k) { return MutationKey{"Position", k, "0.25", {"0.001", "0.6"}, false, {}}; };
    const auto hotkey = [](const char* k, const char* alt) {
        return MutationKey{"Hotkeys", k, alt, {"0xFF", "0x100", "-1"}, true, {}};
    };
    return {
        MutationKey{"Network", "UDPPort", "5000", {"0", "65536"}, false, {}},
        multiplier("YawMultiplier"),
        multiplier("PitchMultiplier"),
        multiplier("RollMultiplier"),
        smooth("LocalSmoothing"),
        smooth("RemoteSmoothing"),
        hotkey("ToggleKey", "0x70"),
        hotkey("PositionToggleKey", "0x71"),
        hotkey("YawModeKey", "0x72"),
        sens("SensitivityX"),
        sens("SensitivityY"),
        sens("SensitivityZ"),
        limit("LimitX"),
        limit("LimitY"),
        limit("LimitZ"),
        limit("LimitZBack"),
        boolean("Position", "Enabled", "false"),
        boolean("General", "AutoEnable", "false"),
        boolean("General", "WorldSpaceYaw", "false"),
        boolean("Crosshair", "Show", "false"),
        boolean("Ship", "AimUIFollowsHead", "true"),
    };
}

// ---------------------------------------------------------------------------
// Comparison 2: the frozen reader against the migration
// ---------------------------------------------------------------------------

// What the mod starts with. Pose shaping and the crosshair switches are not here: the migrated
// build has none, and CheckPoseShaping and CheckReticle hold the import to listing every value
// it leaves out.
struct Startup {
    int port = 0;
    bool enabled = false;
    TrackingMode mode = TrackingMode::RotationAndPosition;
    bool world_yaw = false;
    uint32_t local_smoothing = 0;
    uint32_t remote_smoothing = 0;
    uint32_t limit_x = 0;
    uint32_t limit_y = 0;
    uint32_t limit_y_down = 0;
    uint32_t limit_z = 0;
    uint32_t limit_z_back = 0;
    std::vector<Registration> hotkeys;
};

const cfg::DroppedValue* FindDrop(const std::vector<cfg::DroppedValue>& dropped, cfg::DropRule rule,
                                  const char* section, const char* key) {
    for (const cfg::DroppedValue& d : dropped) {
        if (d.rule == rule && d.section == section && d.key == key) return &d;
    }
    return nullptr;
}

// A hotkey code outside 0x01-0xFE imports as unbound (N1), and only then is it dropped.
bool KeyAfterN1(const std::string& name, int vk, const char* key, const std::vector<cfg::DroppedValue>& dropped) {
    const bool outOfRange = vk != 0 && (vk < 0x01 || vk > 0xFE);
    if (outOfRange != (FindDrop(dropped, cfg::DropRule::KeyCodeOutOfRange, "Hotkeys", key) != nullptr)) {
        Fail(name, std::string("[Hotkeys] ") + key + " dropped as out of range does not match its code");
    }
    return vk != 0 && !outOfRange;
}

// Mod::Initialize at 43f310d, where [Position] Enabled chose between the first two modes and
// ToPositionSettings copied LimitY into both vertical limits, and the keys RegisterBindings
// registered, less a code N1 unbinds.
Startup FromImport(const std::string& name, const legacy::Config& c, const std::vector<cfg::DroppedValue>& dropped) {
    Startup s;
    s.port = c.udpPort;
    s.enabled = c.autoEnable;
    s.mode = c.positionEnabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly;
    s.world_yaw = c.worldSpaceYaw;
    s.local_smoothing = Bits(c.localSmoothing);
    s.remote_smoothing = Bits(c.remoteSmoothing);
    s.limit_x = Bits(c.positionLimitX);
    s.limit_y = Bits(c.positionLimitY);
    s.limit_y_down = Bits(c.positionLimitY);
    s.limit_z = Bits(c.positionLimitZ);
    s.limit_z_back = Bits(c.positionLimitZBack);
    const bool toggleKept = KeyAfterN1(name, c.toggleKey, "ToggleKey", dropped);
    const bool cycleKept = KeyAfterN1(name, c.positionToggleKey, "PositionToggleKey", dropped);
    const bool yawKept = KeyAfterN1(name, c.yawModeKey, "YawModeKey", dropped);
    for (const Registration& r : ImportHotkeys(c)) {
        const bool kept = r.modifiers == kChord || (r.action == Action::Toggle && toggleKept) ||
                          (r.action == Action::CycleMode && cycleKept) || (r.action == Action::YawMode && yawKept);
        if (kept) s.hotkeys.push_back(r);
    }
    return s;
}

// This build: Mod::Initialize, and the lists RegisterBindings registers.
Startup FromMigration(const Config& c) {
    Startup s;
    s.port = c.udp_port;
    s.enabled = c.enable_on_startup;
    s.mode = cameraunlock::DecodeTrackingMode(c.rotation_enabled, c.position_enabled).value();
    s.world_yaw = c.world_space_yaw;
    s.local_smoothing = Bits(c.local_smoothing);
    s.remote_smoothing = Bits(c.remote_smoothing);
    s.limit_x = Bits(c.position.limit_x);
    s.limit_y = Bits(c.position.limit_y);
    s.limit_y_down = Bits(c.position.limit_y_down);
    s.limit_z = Bits(c.position.limit_z);
    s.limit_z_back = Bits(c.position.limit_z_back);
    const std::pair<Action, const std::string*> lists[] = {
        {Action::Toggle, &c.toggle_key_name},
        {Action::CycleMode, &c.cycle_tracking_mode_key_name},
        {Action::YawMode, &c.yaw_mode_key_name},
    };
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        if (!parsed.ok()) throw std::logic_error("a migrated hotkey list does not parse: " + *list);
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            s.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    std::sort(s.hotkeys.begin(), s.hotkeys.end());
    return s;
}

std::vector<std::string> StartupDifferences(const Startup& a, const Startup& b) {
    std::vector<std::string> out;
#define SAME(f) \
    if (a.f != b.f) out.push_back(#f)
    SAME(port);
    SAME(enabled);
    SAME(mode);
    SAME(world_yaw);
    SAME(local_smoothing);
    SAME(remote_smoothing);
    SAME(limit_x);
    SAME(limit_y);
    SAME(limit_y_down);
    SAME(limit_z);
    SAME(limit_z_back);
#undef SAME
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys " + Describe(a.hotkeys) + " against " + Describe(b.hotkeys));
    return out;
}

// Every sensitivity the frozen reader read is listed in its place, folded where it holds the
// 1.0 every build shipped and dropped as PoseShaping where it does not. Returns how many were
// dropped.
int CheckPoseShaping(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    struct Read {
        const char* section;
        const char* key;
        bool shipped;
    };
    const Read reads[] = {
        {"Sensitivity", "YawMultiplier", c.yawMultiplier == legacy::kDefaultMultiplier},
        {"Sensitivity", "PitchMultiplier", c.pitchMultiplier == legacy::kDefaultMultiplier},
        {"Sensitivity", "RollMultiplier", c.rollMultiplier == legacy::kDefaultMultiplier},
        {"Position", "SensitivityX", c.positionSensitivityX == legacy::kDefaultPositionSensitivity},
        {"Position", "SensitivityY", c.positionSensitivityY == legacy::kDefaultPositionSensitivity},
        {"Position", "SensitivityZ", c.positionSensitivityZ == legacy::kDefaultPositionSensitivity},
    };
    if (result.pose_shaping.size() != std::size(reads)) {
        Fail(name, "the import lists " + std::to_string(result.pose_shaping.size()) + " pose-shaping values, not 6");
        return 0;
    }
    int dropped = 0;
    for (size_t k = 0; k < std::size(reads); ++k) {
        const cfg::PoseShapingValue& v = result.pose_shaping[k];
        const std::string label = std::string("[") + reads[k].section + "] " + reads[k].key;
        if (v.section != reads[k].section || v.key != reads[k].key) Fail(name, label + " is not listed in its place");
        if (v.folded != reads[k].shipped) Fail(name, label + " is " + (v.folded ? "folded" : "dropped") + " wrongly");
        const bool listed = FindDrop(result.dropped, cfg::DropRule::PoseShaping, reads[k].section, reads[k].key) != nullptr;
        if (listed == reads[k].shipped) {
            Fail(name, label + (listed ? " is dropped at its shipped value" : " is changed and not dropped"));
        }
        if (!reads[k].shipped) ++dropped;
    }
    return dropped;
}

// The game's crosshair and the ship's aim circle always follow the aim now, so a file that
// switched either off has that switch dropped as a reticle setting, and only then. Returns how
// many were dropped.
int CheckReticle(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    struct Switch {
        const char* section;
        const char* key;
        bool off;
    };
    const Switch switches[] = {
        {"Crosshair", "Show", !c.showCrosshair},
        {"Ship", "AimUIFollowsHead", c.shipAimUIFollowsHead},
    };
    int dropped = 0;
    for (const Switch& sw : switches) {
        const bool listed = FindDrop(result.dropped, cfg::DropRule::Reticle, sw.section, sw.key) != nullptr;
        if (listed != sw.off) {
            Fail(name, std::string("[") + sw.section + "] " + sw.key +
                           (listed ? " is dropped though it left the crosshair following the aim"
                                   : " switched the crosshair off the aim and is not dropped"));
        }
        if (sw.off) ++dropped;
    }
    return dropped;
}

// Every drop the import recorded is by one of the approved rules this map applies.
void CheckDropRules(const std::string& name, const cfg::ImportResult& result) {
    for (const cfg::DroppedValue& d : result.dropped) {
        const bool approved = d.rule == cfg::DropRule::PoseShaping || d.rule == cfg::DropRule::Reticle ||
                              d.rule == cfg::DropRule::KeyCodeOutOfRange;
        if (!approved) Fail(name, "the import drops [" + d.section + "] " + d.key + " by a rule this map never applies");
    }
}

struct MigrationTally {
    std::string committed;
    std::set<std::string> migrated;
    int created = 0;
    int converted = 0;
    int with_pose_shaping_dropped = 0;
    int with_reticle_dropped = 0;
    int with_n1 = 0;
};

std::string Render(const Config& c) {
    return cfg::RenderCanonical(StarfieldHT::MakeConfigTable(), c, {StarfieldHT::kConfigDisplayName});
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

struct Folders {
    std::wstring oracle;
    std::wstring import;
    std::wstring migration;
};

const wchar_t kIniName[] = L"HeadTracking.ini";

void MigrateInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
                  const ImportRun& i, const cfg::ImportResult* result, MigrationTally& tally) {
    using cfg::ConfigLoadStatus;
    EmptyFolder(f.migration);
    const std::wstring path = f.migration + L"\\" + kIniName;
    if (bytes) WriteBytes(path, *bytes);
    cfg::ConfigOwner<Config> owner(StarfieldHT::MakeConfigOwnerOptions(path));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    const std::map<std::wstring, std::string> after = Snapshot(f.migration);

    if (!bytes) {
        ++tally.created;
        if (loaded.status != ConfigLoadStatus::Created) Fail(name, "no file is not Created");
        if (ReadBytes(path) != tally.committed) Fail(name, "the created file is not HeadTracking.ini as committed");
        for (const std::string& d : StartupDifferences(FromImport(name, i.cfg, {}), FromMigration(loaded.config))) {
            Fail(name, "comparison 2: " + d);
        }
        return;
    }

    if (CheckPoseShaping(name, i.cfg, *result) > 0) ++tally.with_pose_shaping_dropped;
    if (CheckReticle(name, i.cfg, *result) > 0) ++tally.with_reticle_dropped;
    CheckDropRules(name, *result);
    if (std::any_of(result->dropped.begin(), result->dropped.end(),
                    [](const cfg::DroppedValue& d) { return d.rule == cfg::DropRule::KeyCodeOutOfRange; })) {
        ++tally.with_n1;
    }

    for (const std::string& d :
         StartupDifferences(FromImport(name, i.cfg, result->dropped), FromMigration(loaded.config))) {
        Fail(name, "comparison 2: " + d);
    }

    ++tally.converted;
    if (loaded.status != ConfigLoadStatus::Migrated) {
        Fail(name, std::string("the migration is ") + cfg::ConfigLoadStatusName(loaded.status) + ": " + loaded.reason);
        return;
    }
    const auto copy = after.find(std::wstring(kIniName) + L".pre-canonical");
    if (copy == after.end() || copy->second != *bytes) Fail(name, ".pre-canonical is not the input");
    if (after.size() != 2) Fail(name, "the migration left files other than the config and its copy");

    const std::string migrated = ReadBytes(path);
    if (Render(loaded.config) != migrated) Fail(name, "rendering the re-read Config does not give the migrated bytes");
    tally.migrated.insert(migrated);

    cfg::ConfigOwner<Config> again(StarfieldHT::MakeConfigOwnerOptions(path));
    const cfg::ConfigLoadResult<Config> reread = again.Load();
    if (reread.status != ConfigLoadStatus::Canonical || !reread.diagnostics.empty() ||
        Render(reread.config) != migrated || Snapshot(f.migration) != after || ReadBytes(path) != migrated) {
        Fail(name, "migrating the migrated file does something");
    }
}

void RunInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
              MigrationTally& tally) {
    OracleRun o;
    {
        EmptyFolder(f.oracle);
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
    }

    ImportRun i;
    std::optional<cfg::ImportResult> result;
    {
        EmptyFolder(f.import);
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (bytes) {
            Config mapped = StarfieldHT::MakeConfigTable().defaults();
            result = StarfieldHT::MakeLegacyImport().run(cfg::LegacyInput{path, Narrow(path), false}, mapped);
        }
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
    MigrateInput(f, name, bytes, i, result ? &*result : nullptr, tally);
}

std::string ReadInput(const std::string& file) {
    return ReadBytes(Widen(std::string(SF_DIFFERENTIAL_INPUTS) + "/" + file));
}

// A file that exists and cannot be opened: the published build ran on its defaults and left it
// alone, the import reports it as such, and the owner defers it on the defaults, saving
// nothing that session.
void TestUnopenableFile(const Folders& f, const std::string& shipped) {
    const std::string name = "a file another program holds open with no sharing";
    OracleRun o;
    ImportRun i;
    std::optional<cfg::ConfigLoadResult<Config>> loaded;
    for (const std::wstring& dir : {f.oracle, f.import, f.migration}) {
        EmptyFolder(dir);
        const std::wstring path = dir + L"\\" + kIniName;
        WriteBytes(path, shipped);
        HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot hold the test file open");
        if (dir == f.oracle) {
            o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
        } else if (dir == f.import) {
            i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        } else {
            cfg::ConfigOwner<Config> owner(StarfieldHT::MakeConfigOwnerOptions(path));
            loaded.emplace(owner.Load());
            if (owner.Save([](Config& c) { c.world_space_yaw = false; }).status != cfg::ConfigSaveStatus::NotSaved) {
                Fail(name, "a deferred session saved");
            }
        }
        CloseHandle(held);
        if (ReadBytes(path) != shipped) Fail(name, "a build rewrote a file it could not open");
    }
    if (o.status != oracle_api::LoadStatus::OpenFailed) Fail(name, "the published build did not report it unopenable");
    if (i.status != legacy::ReadStatus::OpenFailed) Fail(name, "the import did not report it unopenable");
    CompareOracleWithImport(name, o, i);
    if (loaded->status != cfg::ConfigLoadStatus::Deferred) {
        Fail(name, std::string("the owner's load is ") + cfg::ConfigLoadStatusName(loaded->status) + ", not Deferred");
    }
    for (const std::string& d : StartupDifferences(FromImport(name, i.cfg, {}), FromMigration(loaded->config))) {
        Fail(name, "comparison 2: " + d);
    }
    if (Snapshot(f.migration) != std::map<std::wstring, std::string>{{kIniName, shipped}}) {
        Fail(name, "a deferred file did not keep its bytes, or got a copy");
    }
}

// Registration compares the two builds by key and modifiers, which holds only while a binding
// with no modifiers fires as the old build's NavGuarded did (not while Ctrl and Shift are both
// held) and a Ctrl+Shift binding as its ChordGuarded did (while both are held). Alt changes
// neither.
void TestRegistrationModel() {
    using cameraunlock::input::detail::BindingFires;
    for (unsigned held = 0; held < 8; ++held) {
        const auto mods = static_cast<KeyModifiers>(held);
        const bool chordHeld = cameraunlock::input::HasModifiers(mods, KeyModifiers::kCtrl | KeyModifiers::kShift);
        if (BindingFires(KeyModifiers::kNone, mods) != !chordHeld) {
            Fail("registration", "a key with no modifiers does not fire as NavGuarded did, held " + std::to_string(held));
        }
        if (BindingFires(KeyModifiers::kCtrl | KeyModifiers::kShift, mods) != chordHeld) {
            Fail("registration", "a Ctrl+Shift key does not fire as ChordGuarded did, held " + std::to_string(held));
        }
    }
}

void TestFrozenDefaults() {
    const oracle_api::Config o = oracle_api::Defaults();
    const legacy::Config l;
    for (const std::string& field : SharedFieldDifferences(o, l)) {
        Fail("defaults", field + ": the frozen default differs from the published build's");
    }
}

}  // namespace

int main() {
    try {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::wstring root = std::wstring(temp) + L"starfield-config-differential-" +
                                  std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(root.c_str(), nullptr);
        const Folders folders{MakeFolder(root, L"oracle"), MakeFolder(root, L"import"),
                              MakeFolder(root, L"migration")};
        MigrationTally tally;
        tally.committed = ReadBytes(Widen(SF_COMMITTED_CONFIG));

        TestFrozenDefaults();
        TestRegistrationModel();

        const std::string shipped = ReadInput("shipped-dev.ini");
        const std::string firstRun = ReadInput("first-run-dev.ini");
        {
            EmptyFolder(folders.oracle);
            const std::wstring path = folders.oracle + L"\\" + kIniName;
            oracle_api::Config created;
            if (oracle_api::LoadOrCreate(Narrow(path).c_str(), created) != oracle_api::LoadStatus::Created ||
                ReadBytes(path) != firstRun) {
                Fail("first run", "the oracle's first-run output is not inputs/first-run-dev.ini");
            }
        }

        const std::vector<std::pair<std::string, std::optional<std::string>>> inputs = {
            {"no file", std::nullopt},
            {"empty file", std::string()},
            {"shipped, dev", shipped},
            {"first run, dev", firstRun},
        };
        for (const auto& [name, bytes] : inputs) RunInput(folders, name, bytes, tally);
        TestUnopenableFile(folders, shipped);

        // Fresh equals upgrade: the file the published build shipped and the one it wrote at
        // first launch both convert to the committed file, as no file is created as it.
        for (const std::string& file : {shipped, firstRun}) {
            EmptyFolder(folders.migration);
            const std::wstring path = folders.migration + L"\\" + kIniName;
            WriteBytes(path, file);
            cfg::ConfigOwner<Config> owner(StarfieldHT::MakeConfigOwnerOptions(path));
            owner.Load();
            if (ReadBytes(path) != tally.committed) {
                Fail("fresh equals upgrade", "a published build's default file does not convert to the committed file");
            }
        }

        const std::vector<IniMutation> corpus = GenerateIniMutations(shipped, legacy::ReadKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(folders, "corpus: " + m.name, m.bytes, tally);

        std::printf("%zu inputs, %zu of them from the corpus\n", inputs.size() + 1 + corpus.size(), corpus.size());
        std::printf("comparison 1, the published build against the frozen reader:\n");
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }
        std::printf("comparison 2, the frozen reader against the migration: %d created, %d converted, "
                    "%zu distinct files\n",
                    tally.created, tally.converted, tally.migrated.size());
        std::printf("  %d with a changed sensitivity dropped (pose_shaping)\n", tally.with_pose_shaping_dropped);
        std::printf("  %d with the crosshair or ship aim circle switched off the aim dropped (reticle)\n",
                    tally.with_reticle_dropped);
        std::printf("  %d with a hotkey code outside 0x01-0xFE unbound (N1)\n", tally.with_n1);
        if (tally.with_pose_shaping_dropped == 0) Fail("pose shaping", "no input drops a changed value");
        if (tally.with_reticle_dropped == 0) Fail("reticle", "no input drops a crosshair switch");
        if (tally.migrated.count(tally.committed) == 0) Fail("first run", "no input migrated to the committed file");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring lintDir(exe);
        lintDir = lintDir.substr(0, lintDir.find_last_of(L'\\'));
        lintDir = MakeFolder(lintDir, L"migrated");
        int n = 0;
        for (const std::string& file : tally.migrated) {
            WriteBytes(lintDir + L"\\" + std::to_wstring(n++) + L".ini", file);
        }

        for (const std::wstring& dir : {folders.oracle, folders.import, folders.migration}) {
            EmptyFolder(dir);
            RemoveDirectoryW(dir.c_str());
        }
        RemoveDirectoryW(root.c_str());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config differential: all passed\n");
        return 0;
    }
    std::printf("config differential: %d failure(s)\n", g_failures);
    return 1;
}
