// The config differential test. Every input is read two ways:
//
//   oracle     the published build's reader (oracle/), the dev pre-release at d276538, and
//              its startup code
//   import     the frozen reader in src/legacy_config/, and the startup code it runs under
//
// Comparison 1, oracle against import, finds what a player updating from the published build
// sees change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Inputs: no file, an empty file, the HeadTracking.ini the dev pre-release shipped (its
// installer ZIP's plugins\HeadTracking.ini and its launcher seed are the same bytes, and the
// only version of the file committed up to it), the file that build writes at first launch
// when there is none (extracted once into inputs/), and core's corpus over the shipped file.
// v0.0.1 is a tag with no release; its reader, its shipped file and its core pin are the dev
// build's.

#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = StarfieldHT::legacy;
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
// The run
// ---------------------------------------------------------------------------

struct Folders {
    std::wstring oracle;
    std::wstring import;
};

const wchar_t kIniName[] = L"HeadTracking.ini";

void RunInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes) {
    OracleRun o;
    {
        EmptyFolder(f.oracle);
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
    }

    ImportRun i;
    {
        EmptyFolder(f.import);
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
}

std::string ReadInput(const std::string& file) {
    return ReadBytes(Widen(std::string(SF_DIFFERENTIAL_INPUTS) + "/" + file));
}

// A file that exists and cannot be opened: the published build ran on its defaults and left it
// alone, and the import reports it as such.
void TestUnopenableFile(const Folders& f, const std::string& shipped) {
    const std::string name = "a file another program holds open with no sharing";
    OracleRun o;
    ImportRun i;
    for (const std::wstring& dir : {f.oracle, f.import}) {
        EmptyFolder(dir);
        const std::wstring path = dir + L"\\" + kIniName;
        WriteBytes(path, shipped);
        HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot hold the test file open");
        if (dir == f.oracle) {
            o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
        } else {
            i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        }
        CloseHandle(held);
        if (ReadBytes(path) != shipped) Fail(name, "a build rewrote a file it could not open");
    }
    if (o.status != oracle_api::LoadStatus::OpenFailed) Fail(name, "the published build did not report it unopenable");
    if (i.status != legacy::ReadStatus::OpenFailed) Fail(name, "the import did not report it unopenable");
    CompareOracleWithImport(name, o, i);
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
        const Folders folders{MakeFolder(root, L"oracle"), MakeFolder(root, L"import")};

        TestFrozenDefaults();

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
        for (const auto& [name, bytes] : inputs) RunInput(folders, name, bytes);
        TestUnopenableFile(folders, shipped);

        const std::vector<IniMutation> corpus = GenerateIniMutations(shipped, legacy::ReadKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(folders, "corpus: " + m.name, m.bytes);

        std::printf("%zu inputs, %zu of them from the corpus\n", inputs.size() + 1 + corpus.size(), corpus.size());
        std::printf("comparison 1, the published build against the frozen reader:\n");
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }

        for (const std::wstring& dir : {folders.oracle, folders.import}) {
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
