// The config differential test. Every input is read three ways:
//
//   oracle     the published build's reader (oracle/), the dev pre-release at d276538, and
//              its startup code
//   import     the frozen reader in src/legacy_config/, and the startup code it ran under
//   migration  the config owner on a folder holding the input as HeadTracking.ini: the import
//              into a new CameraUnlock.ini, then the canonical reader and table on that file,
//              and the startup code of this build. Every owner reads a scratch Defaults.ini,
//              never the developer's own
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
// canonical rows hold, so no input is deferred and no N1 or N2 applies. A hotkey on Ctrl, Shift
// or Alt alone (0x10-0x12, 0xA0-0xA5) is unbound and dropped (N3); its chord stays.
//
// A row the player never changed from what the published build shipped follows Defaults.ini:
// the import lists it in follows_defaults_ini and the migration writes it default, the tracking
// mode pair as one unit. The test derives the untouched rows from how the import's startup
// differs from the startup with no file, and holds the import's list to them on every input.
//
// Each input migrates three times: over a Defaults.ini at the built-in values, from a read-only
// HeadTracking.ini, and over a Defaults.ini that differs from the built-in values on every global
// row. The first two give the settings the import read, since the published build's defaults
// are the built-in values. Over the third a row the player never changed is written default and
// takes Defaults.ini's value, and a changed row keeps the player's. Every load leaves
// HeadTracking.ini's bytes, last write time and attributes as they were, the folder holds
// HeadTracking.ini and CameraUnlock.ini and nothing else, and the next start reads
// CameraUnlock.ini, imports nothing and writes nothing.
//
// The distinct migrated files are written beside the executable under migrated\, for
// lint-migrated.mjs to run core's canonical config lint over.
//
// Inputs: no file, an empty file, the HeadTracking.ini the dev pre-release shipped (its
// installer ZIP's plugins\HeadTracking.ini and its launcher seed are the same bytes, and the
// only version of the file committed up to it), the file that build writes at first launch
// when there is none (extracted once into inputs/), core's corpus over the shipped file, and
// the shipped file with all three hotkeys on each code from 0x01 to 0xFE.
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

// The names of the files in the folder.
std::set<std::wstring> Names(const std::wstring& dir) {
    std::set<std::wstring> names;
    for (const auto& [name, bytes] : Snapshot(dir)) names.insert(name);
    return names;
}

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    uint64_t written = 0;
    DWORD attributes = 0;

    bool operator==(const FileState& o) const {
        return bytes == o.bytes && written == o.written && attributes == o.attributes;
    }
    bool operator!=(const FileState& o) const { return !(*this == o); }
};

std::optional<FileState> StateOf(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + Narrow(path));
    }
    FileState state;
    state.bytes = ReadBytes(path);
    state.written = (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    return std::any_of(log.begin(), log.end(),
                       [&text](const std::string& line) { return line.find(text) != std::string::npos; });
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
// The chord modifiers this build registers: left Ctrl is Starfield's sneak key.
constexpr unsigned kGameChord = static_cast<unsigned>(KeyModifiers::kShift | KeyModifiers::kAlt);

std::string Describe(const std::vector<Registration>& regs) {
    std::string out;
    for (const Registration& r : regs) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%s%s:%s0x%02X", out.empty() ? "" : " ", ActionName(r.action),
                      r.modifiers == kChord ? "Ctrl+Shift+" : r.modifiers == kGameChord ? "Shift+Alt+" : "",
                      static_cast<unsigned>(r.vk));
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

// What a legacy file's hotkeys become: the code each action had, as the build that carries the
// frozen reader registered it (src/hooks/input_hook.cpp at 43f310d), and this game's own chords in
// place of the Ctrl+Shift ones that build registered beside them ("chords" below).
std::vector<Registration> ImportHotkeys(const legacy::Config& c) {
    std::vector<Registration> regs = {
        {Action::Toggle, c.toggleKey, kNav},
        {Action::CycleMode, c.positionToggleKey, kNav},
        {Action::YawMode, c.yawModeKey, kNav},
        {Action::Toggle, 'Y', kGameChord},
        {Action::CycleMode, 'T', kGameChord},
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
    {"chords", "core 0ca6183",
     "the Ctrl+Shift chords are gone: left Ctrl is the game's sneak key, and the game acts on G "
     "(grenade) and H (status) whatever is held with them. Toggle is Shift+Alt+Y, the tracking "
     "mode cycle Shift+Alt+T, and the yaw mode has no chord"},
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
        } else if (r.modifiers != kChord) {
            expected.push_back(r);
        } else if (r.action == Action::Toggle) {
            expected.push_back({Action::Toggle, 'Y', kGameChord});
        } else if (r.action == Action::CycleMode) {
            expected.push_back({Action::CycleMode, 'T', kGameChord});
        }
    }
    std::sort(expected.begin(), expected.end());
    if (sightsBound) ++Listed("sights-key").seen;
    ++Listed("chords").seen;
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

// A hotkey code outside 0x01-0xFE imports as unbound (N1), and so does one on Ctrl, Shift or Alt
// alone (N3); only then is it dropped.
bool KeyAfterN1(const std::string& name, int vk, const char* key, const std::vector<cfg::DroppedValue>& dropped) {
    const bool outOfRange = vk != 0 && (vk < 0x01 || vk > 0xFE);
    const bool modifier = (vk >= 0x10 && vk <= 0x12) || (vk >= 0xA0 && vk <= 0xA5);
    if (outOfRange != (FindDrop(dropped, cfg::DropRule::KeyCodeOutOfRange, "Hotkeys", key) != nullptr)) {
        Fail(name, std::string("[Hotkeys] ") + key + " dropped as out of range does not match its code");
    }
    if (modifier != (FindDrop(dropped, cfg::DropRule::ModifierKey, "Hotkeys", key) != nullptr)) {
        Fail(name, std::string("[Hotkeys] ") + key + " dropped as a modifier key does not match its code");
    }
    return vk != 0 && !outOfRange && !modifier;
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
        const bool kept = r.modifiers == kGameChord || (r.action == Action::Toggle && toggleKept) ||
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
                              d.rule == cfg::DropRule::KeyCodeOutOfRange || d.rule == cfg::DropRule::ModifierKey;
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
    int with_n3 = 0;
    int with_row_changed = 0;
    int with_mode_changed = 0;
};

// Every row's value, whatever Defaults.ini holds, so two Configs compare in full.
std::string Values(const Config& c) {
    return cfg::RenderCanonical(StarfieldHT::MakeConfigTable(), c, {StarfieldHT::kConfigDisplayName});
}

// ---------------------------------------------------------------------------
// Rows that follow Defaults.ini
// ---------------------------------------------------------------------------

using Concept = cfg::schema::Concept;

// Every row the table binds that follows Defaults.ini. The four hotkey rows are the game's own
// (per_game) and do not: a legacy key is written to them as a value whatever it was.
const std::set<Concept>& AllRows() {
    static const std::set<Concept> all = {
        Concept::UdpPort,           Concept::EnableOnStartup,      Concept::WorldSpaceYaw,
        Concept::RotationEnabled,   Concept::LocalSmoothing,       Concept::RemoteSmoothing,
        Concept::PositionEnabled,   Concept::PositionLimitX,       Concept::PositionLimitY,
        Concept::PositionLimitYDown, Concept::PositionLimitZ,      Concept::PositionLimitZBack,
        Concept::LightMultiplier,   Concept::TrueFreeLook,         Concept::FreeLookMarker,
        Concept::StockSights,
    };
    return all;
}

std::vector<Registration> KeysOf(const std::vector<Registration>& hotkeys, Action action) {
    std::vector<Registration> out;
    for (const Registration& r : hotkeys) {
        if (r.action == action) out.push_back(r);
    }
    return out;
}

// The rows the player never changed: each reads in the import's startup as it does with no file.
// The mode pair is both rows or neither, and the published build had no light setting and no
// true free look.
std::set<Concept> UntouchedRows(const Startup& imported, const Startup& none) {
    std::set<Concept> changed;
    const auto row = [&changed](bool same, Concept c) {
        if (!same) changed.insert(c);
    };
    row(imported.port == none.port, Concept::UdpPort);
    row(imported.enabled == none.enabled, Concept::EnableOnStartup);
    row(imported.world_yaw == none.world_yaw, Concept::WorldSpaceYaw);
    row(imported.mode == none.mode, Concept::RotationEnabled);
    row(imported.mode == none.mode, Concept::PositionEnabled);
    row(imported.local_smoothing == none.local_smoothing, Concept::LocalSmoothing);
    row(imported.remote_smoothing == none.remote_smoothing, Concept::RemoteSmoothing);
    row(imported.limit_x == none.limit_x, Concept::PositionLimitX);
    row(imported.limit_y == none.limit_y, Concept::PositionLimitY);
    row(imported.limit_y_down == none.limit_y_down, Concept::PositionLimitYDown);
    row(imported.limit_z == none.limit_z, Concept::PositionLimitZ);
    row(imported.limit_z_back == none.limit_z_back, Concept::PositionLimitZBack);
    std::set<Concept> untouched;
    for (const Concept c : AllRows()) {
        if (!changed.count(c)) untouched.insert(c);
    }
    return untouched;
}

std::string RowNames(const std::set<Concept>& rows) {
    std::string text;
    for (const Concept c : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<size_t>(c)].name);
    }
    return text.empty() ? "none" : text;
}

// `c` with every row in `follows` as `defaults_ini` gives it: what the session runs on over that
// Defaults.ini.
Config OverDefaults(Config c, const std::set<Concept>& follows, const Config& defaults_ini) {
    for (const Concept row : follows) {
        switch (row) {
            case Concept::UdpPort: c.udp_port = defaults_ini.udp_port; break;
            case Concept::EnableOnStartup: c.enable_on_startup = defaults_ini.enable_on_startup; break;
            case Concept::WorldSpaceYaw: c.world_space_yaw = defaults_ini.world_space_yaw; break;
            case Concept::RotationEnabled: c.rotation_enabled = defaults_ini.rotation_enabled; break;
            case Concept::PositionEnabled: c.position_enabled = defaults_ini.position_enabled; break;
            case Concept::LocalSmoothing:
                c.local_smoothing = defaults_ini.local_smoothing;
                c.position.local_smoothing = defaults_ini.position.local_smoothing;
                break;
            case Concept::RemoteSmoothing:
                c.remote_smoothing = defaults_ini.remote_smoothing;
                c.position.remote_smoothing = defaults_ini.position.remote_smoothing;
                break;
            case Concept::PositionLimitX: c.position.limit_x = defaults_ini.position.limit_x; break;
            case Concept::PositionLimitY: c.position.limit_y = defaults_ini.position.limit_y; break;
            case Concept::PositionLimitYDown: c.position.limit_y_down = defaults_ini.position.limit_y_down; break;
            case Concept::PositionLimitZ: c.position.limit_z = defaults_ini.position.limit_z; break;
            case Concept::PositionLimitZBack: c.position.limit_z_back = defaults_ini.position.limit_z_back; break;
            case Concept::ToggleKey: c.toggle_key_name = defaults_ini.toggle_key_name; break;
            case Concept::CycleTrackingModeKey:
                c.cycle_tracking_mode_key_name = defaults_ini.cycle_tracking_mode_key_name;
                break;
            case Concept::YawModeKey: c.yaw_mode_key_name = defaults_ini.yaw_mode_key_name; break;
            case Concept::LightMultiplier: c.light.multiplier = defaults_ini.light.multiplier; break;
            case Concept::TrueFreeLook: c.true_free_look = defaults_ini.true_free_look; break;
            case Concept::FreeLookMarker: c.free_look_marker = defaults_ini.free_look_marker; break;
            case Concept::StockSights: c.stock_sights = defaults_ini.stock_sights; break;
            case Concept::TrueFreeLookKey: c.true_free_look_key_name = defaults_ini.true_free_look_key_name; break;
            default: throw std::logic_error("a row the table does not bind follows Defaults.ini");
        }
    }
    return c;
}

// A row, or both rows of the tracking mode, which Defaults.ini gives as one unit.
std::set<Concept> Pair(Concept row) {
    if (row == Concept::RotationEnabled || row == Concept::PositionEnabled) {
        return {Concept::RotationEnabled, Concept::PositionEnabled};
    }
    return {row};
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

struct Folders {
    std::wstring oracle;
    std::wstring import;
    std::wstring migration;
    std::wstring read_only;
    std::wstring skewed;
    // Defaults.ini at the built-in values, which the first owner creates, and one that differs
    // from them on every global row the table binds.
    std::wstring defaults;
    std::wstring skewed_defaults;
    // What the skewed Defaults.ini sets every row to.
    Config skewed_values;
};

const wchar_t kIniName[] = L"HeadTracking.ini";
const wchar_t kCanonicalName[] = L"CameraUnlock.ini";

const char kSkewedDefaults[] =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=true\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.5\r\n\r\n"
    "[Position]\r\nPositionEnabled=false\r\nTrueFreeLook=true\r\nFreeLookMarker=true\r\nStockSights=true\r\nPositionLimitX=0.25\r\nPositionLimitY=0.25\r\n"
    "PositionLimitYDown=0.25\r\nPositionLimitZ=0.25\r\nPositionLimitZBack=0.25\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\nTrueFreeLookKey=F11\r\n\r\n"
    "[Light]\r\nLightMultiplier=1.0\r\n";

cfg::ConfigOwnerOptions<Config> Options(const std::wstring& folder, const std::wstring& defaults) {
    return StarfieldHT::MakeConfigOwnerOptions(folder + L"\\", cfg::DefaultsFile::At(defaults));
}

// The owner on `folder`, which holds the input as HeadTracking.ini (or nothing), over the
// Defaults.ini at `defaults`: what the design asks of the load beyond comparison 2, and the
// settings the session runs on. A file it migrates goes into the tally.
std::optional<Config> Migrate(const std::wstring& folder, const std::wstring& defaults, const std::string& label,
                              MigrationTally& tally) {
    using cfg::ConfigLoadStatus;
    const std::wstring legacyPath = folder + L"\\" + kIniName;
    const std::wstring path = folder + L"\\" + kCanonicalName;
    const std::optional<FileState> legacyBefore = StateOf(legacyPath);

    const cfg::ConfigLoadResult<Config> loaded = cfg::ConfigOwner<Config>(Options(folder, defaults)).Load();
    if (StateOf(legacyPath) != legacyBefore) Fail(label, "the load changed HeadTracking.ini's bytes, write time or attributes");

    if (!legacyBefore) {
        if (loaded.status != ConfigLoadStatus::Created) Fail(label, "no file is not Created");
        if (Names(folder) != std::set<std::wstring>{kCanonicalName}) Fail(label, "a first launch left more than CameraUnlock.ini");
    } else if (loaded.status != ConfigLoadStatus::Migrated) {
        Fail(label, std::string("the migration is ") + cfg::ConfigLoadStatusName(loaded.status) + ": " + loaded.reason);
        return std::nullopt;
    } else {
        if (Names(folder) != std::set<std::wstring>{kIniName, kCanonicalName}) {
            Fail(label, "the folder holds more than HeadTracking.ini and CameraUnlock.ini");
        }
        const std::string migrated = ReadBytes(path);
        const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(migrated);
        Config reread = StarfieldHT::MakeConfigTable().defaults();
        if (!cfg::HasCanonicalStamp(migrated) || !doc.IsReadable() || !doc.diagnostics.empty() ||
            !cfg::ApplyCanonical(doc, StarfieldHT::MakeConfigTable(), reread).diagnostics.empty()) {
            Fail(label, "the migrated file is not a stamped canonical file that reads without a diagnostic");
        }
        tally.migrated.insert(migrated);
    }

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(path);
    const cfg::ConfigLoadResult<Config> again = cfg::ConfigOwner<Config>(Options(folder, defaults)).Load();
    if (again.status != ConfigLoadStatus::Canonical || !again.diagnostics.empty()) {
        Fail(label, std::string("the next start is ") + cfg::ConfigLoadStatusName(again.status));
    }
    if (Values(again.config) != Values(loaded.config)) Fail(label, "the next start gives other settings");
    if (StateOf(path) != created || StateOf(legacyPath) != legacyBefore) Fail(label, "the next start changed a file");
    if (legacyBefore && !LogSays(again.log, "is left as it was and is not read")) {
        Fail(label, "the next start does not log that HeadTracking.ini is not read");
    }
    return loaded.config;
}

// The inputs no player edited: every row follows Defaults.ini, and the migration gives the
// committed file.
bool IsUnedited(const std::string& name) {
    return name == "empty file" || name == "shipped, dev" || name == "first run, dev";
}

void MigrateInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
                  const ImportRun& i, const cfg::ImportResult* result, MigrationTally& tally) {
    EmptyFolder(f.migration);
    if (bytes) WriteBytes(f.migration + L"\\" + kIniName, *bytes);
    const std::optional<Config> migrated = Migrate(f.migration, f.defaults, name, tally);
    if (!migrated) return;

    if (!bytes) {
        ++tally.created;
        if (ReadBytes(f.migration + L"\\" + kCanonicalName) != tally.committed) {
            Fail(name, "the created CameraUnlock.ini is not HeadTracking.ini as committed");
        }
        for (const std::string& d : StartupDifferences(FromImport(name, i.cfg, {}), FromMigration(*migrated))) {
            Fail(name, "comparison 2: " + d);
        }
        return;
    }

    ++tally.converted;
    const std::set<Concept> follows(result->follows_defaults_ini.begin(), result->follows_defaults_ini.end());
    if (follows.size() != result->follows_defaults_ini.size()) Fail(name, "follows_defaults_ini names a row twice");
    const std::set<Concept> untouched =
        UntouchedRows(FromImport(name, i.cfg, result->dropped), FromImport(name, legacy::Config{}, {}));
    if (follows != untouched) {
        Fail(name, "the rows left to Defaults.ini are " + RowNames(follows) + ", the untouched rows " +
                       RowNames(untouched));
    }
    if (untouched != AllRows()) ++tally.with_row_changed;
    if (!untouched.count(Concept::RotationEnabled)) ++tally.with_mode_changed;
    if (IsUnedited(name)) {
        if (untouched != AllRows()) Fail(name, "an input no player edited changes a row");
        if (ReadBytes(f.migration + L"\\" + kCanonicalName) != tally.committed) {
            Fail(name, "an input no player edited does not migrate to HeadTracking.ini as committed");
        }
    }
    if (CheckPoseShaping(name, i.cfg, *result) > 0) ++tally.with_pose_shaping_dropped;
    if (CheckReticle(name, i.cfg, *result) > 0) ++tally.with_reticle_dropped;
    CheckDropRules(name, *result);
    if (std::any_of(result->dropped.begin(), result->dropped.end(),
                    [](const cfg::DroppedValue& d) { return d.rule == cfg::DropRule::KeyCodeOutOfRange; })) {
        ++tally.with_n1;
    }
    if (std::any_of(result->dropped.begin(), result->dropped.end(),
                    [](const cfg::DroppedValue& d) { return d.rule == cfg::DropRule::ModifierKey; })) {
        ++tally.with_n3;
    }
    for (const std::string& d : StartupDifferences(FromImport(name, i.cfg, result->dropped), FromMigration(*migrated))) {
        Fail(name, "comparison 2: " + d);
    }

    EmptyFolder(f.read_only);
    const std::wstring readOnly = f.read_only + L"\\" + kIniName;
    WriteBytes(readOnly, *bytes);
    SetFileAttributesW(readOnly.c_str(), FILE_ATTRIBUTE_READONLY);
    const std::optional<Config> fromReadOnly = Migrate(f.read_only, f.defaults, name + " (read-only)", tally);
    if (fromReadOnly && Values(*fromReadOnly) != Values(*migrated)) {
        Fail(name, "a read-only HeadTracking.ini migrates to other settings than a writable one");
    }

    EmptyFolder(f.skewed);
    WriteBytes(f.skewed + L"\\" + kIniName, *bytes);
    const std::optional<Config> overSkewed = Migrate(f.skewed, f.skewed_defaults, name + " (skewed Defaults.ini)", tally);
    if (overSkewed && Values(*overSkewed) != Values(OverDefaults(*migrated, follows, f.skewed_values))) {
        Fail(name, "over a Defaults.ini that differs on every global row, an untouched row does not take its value "
                   "or a changed row does not keep the player's");
    }
    if (overSkewed) {
        const std::string written = ReadBytes(f.skewed + L"\\" + kCanonicalName);
        // A changed row is written default only where the player's value is what this
        // Defaults.ini gives.
        for (const Concept row : AllRows()) {
            const std::string key = cfg::schema::kConcepts[static_cast<size_t>(row)].key;
            const bool writtenDefault = written.find("\r\n" + key + "=default\r\n") != std::string::npos;
            const bool asDefaultsIni = Values(OverDefaults(*migrated, Pair(row), f.skewed_values)) == Values(*migrated);
            if (follows.count(row) ? !writtenDefault : writtenDefault != asDefaultsIni) {
                Fail(name, key + (follows.count(row) ? " follows Defaults.ini and is not written default"
                                                     : " was changed and is written default though Defaults.ini "
                                                       "gives another value, or the other way round"));
            }
        }
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

// `text` with the value of its one `key=` line replaced, the rest of that line included.
std::string WithValue(const std::string& text, const std::string& key, const std::string& value) {
    const size_t at = text.find("\n" + key + "=");
    if (at == std::string::npos || text.find("\n" + key + "=", at + 1) != std::string::npos) {
        throw std::logic_error("the shipped file does not hold exactly one " + key + " line");
    }
    const size_t start = at + 1 + key.size() + 1;
    const size_t end = text.find_first_of("\r\n", start);
    return text.substr(0, start) + value + text.substr(end);
}

// Every hotkey code the frozen reader accepts, 0x01 to 0xFE, on all three hotkeys at once. The
// corpus tries one alternate code per hotkey; this is where the codes the key table has no name
// for have to come through the migration as keys, and a Ctrl, Shift or Alt key alone as unbound.
std::vector<std::pair<std::string, std::string>> EveryHotkeyCode(const std::string& shipped) {
    std::vector<std::pair<std::string, std::string>> inputs;
    for (int vk = 0x01; vk <= 0xFE; ++vk) {
        char code[8];
        std::snprintf(code, sizeof(code), "0x%02X", static_cast<unsigned>(vk));
        std::string bytes = shipped;
        for (const char* key : {"ToggleKey", "PositionToggleKey", "YawModeKey"}) bytes = WithValue(bytes, key, code);
        inputs.emplace_back(std::string("every hotkey ") + code, bytes);
    }
    return inputs;
}

// A file that exists and cannot be opened: the published build ran on its defaults and left it
// alone, the import reports it as such, and the owner defers it on the defaults, creates no
// CameraUnlock.ini and saves nothing that session.
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
            cfg::ConfigOwner<Config> owner(Options(dir, f.defaults));
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
        Fail(name, "a deferred import left more than HeadTracking.ini as it was");
    }
    if (!LogSays(loaded->log, "in use by another program") && loaded->reason.find("in use by another program") == std::string::npos) {
        Fail(name, "the deferred import does not say the file is in use");
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
        const bool gameChordHeld = cameraunlock::input::HasModifiers(mods, KeyModifiers::kShift | KeyModifiers::kAlt);
        if (BindingFires(KeyModifiers::kShift | KeyModifiers::kAlt, mods) != gameChordHeld) {
            Fail("registration", "a Shift+Alt key does not fire exactly while both are held, held " + std::to_string(held));
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
        const std::wstring global = MakeFolder(root, L"global");
        const std::wstring skewedGlobal = MakeFolder(root, L"skewed-global");
        Folders folders{MakeFolder(root, L"oracle"),    MakeFolder(root, L"import"),
                        MakeFolder(root, L"migration"), MakeFolder(root, L"read-only"),
                        MakeFolder(root, L"skewed"),    global + L"\\Defaults.ini",
                        skewedGlobal + L"\\Defaults.ini", StarfieldHT::MakeConfigTable().defaults()};
        WriteBytes(folders.skewed_defaults, kSkewedDefaults);
        {
            const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(kSkewedDefaults);
            if (!doc.IsReadable() || !doc.diagnostics.empty() ||
                !cfg::ApplyCanonical(doc, StarfieldHT::MakeConfigTable(), folders.skewed_values).diagnostics.empty()) {
                Fail("skewed Defaults.ini", "it does not set every row without a diagnostic");
            }
            const Config builtIn = StarfieldHT::MakeConfigTable().defaults();
            for (const Concept row : AllRows()) {
                if (Values(OverDefaults(builtIn, Pair(row), folders.skewed_values)) == Values(builtIn)) {
                    Fail("skewed Defaults.ini", std::string(cfg::schema::kConcepts[static_cast<size_t>(row)].name) +
                                                    " is at its built-in value");
                }
            }
        }
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
        // first launch both import into a CameraUnlock.ini that is the committed file, as no file
        // is created as it, over a Defaults.ini at the built-in values.
        for (const std::string& file : {shipped, firstRun}) {
            EmptyFolder(folders.migration);
            WriteBytes(folders.migration + L"\\" + kIniName, file);
            cfg::ConfigOwner<Config> owner(Options(folders.migration, folders.defaults));
            if (owner.Load().status != cfg::ConfigLoadStatus::Migrated ||
                ReadBytes(folders.migration + L"\\" + kCanonicalName) != tally.committed) {
                Fail("fresh equals upgrade", "a published build's default file does not import into the committed file");
            }
        }

        const std::vector<IniMutation> corpus = GenerateIniMutations(shipped, legacy::ReadKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(folders, "corpus: " + m.name, m.bytes, tally);

        const auto codes = EveryHotkeyCode(shipped);
        for (const auto& [name, bytes] : codes) RunInput(folders, name, bytes, tally);

        std::printf("%zu inputs, %zu of them from the corpus and %zu with every hotkey on one code\n",
                    inputs.size() + 1 + corpus.size() + codes.size(), corpus.size(), codes.size());
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
        std::printf("  %d with a hotkey on Ctrl, Shift or Alt alone unbound (N3)\n", tally.with_n3);
        std::printf("  %d with a row changed from the published build's default, %d of them the tracking mode\n",
                    tally.with_row_changed, tally.with_mode_changed);
        if (tally.with_n3 == 0) Fail("N3", "no input binds a hotkey to a modifier key alone");
        if (tally.with_row_changed == 0 || tally.with_mode_changed == 0) {
            Fail("follows Defaults.ini", "no input changes a row, or none the tracking mode");
        }
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

        for (const std::wstring& dir : {folders.oracle, folders.import, folders.migration, folders.read_only,
                                        folders.skewed, global, skewedGlobal}) {
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
