// The settings file: the committed HeadTracking.ini is the table's render, the defaults every
// published build ran on map to it, the toggles save only their own lines, and End's row cannot
// be saved.
//
// `--render-config <path>` writes the committed file instead (pixi run render-config).

#include "core/config.h"

#include "legacy_config/legacy_config.h"

#include <cameraunlock/tracking/tracking_mode.h>

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace StarfieldHT;

namespace {

namespace cfg = cameraunlock::config;

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read a test file");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write a test file");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

std::string Rendered() {
    const auto table = MakeConfigTable();
    return cfg::RenderCanonical(table, table.defaults(), {kConfigDisplayName});
}

void TestCommittedConfigIsRendered() {
    Check(ReadBytes(Widen(SF_COMMITTED_CONFIG)) == Rendered(),
          "HeadTracking.ini is the table rendered from its defaults (pixi run render-config)");
}

// A fresh install and an upgrade from the published build's defaults start the same: the map of
// the frozen defaults holds every row at the table's default, and every sensitivity they hold
// is the shipped one, which was identity.
void TestLegacyDefaultsMapToTheDefaults() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring missing = std::wstring(temp) + L"starfield-no-such-folder\\" + Widen(kConfigFileName);
    const auto table = MakeConfigTable();
    Config mapped = table.defaults();
    const int size = WideCharToMultiByte(CP_ACP, 0, missing.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string ansi(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, missing.c_str(), -1, ansi.data(), size, nullptr, nullptr);
    ansi.resize(static_cast<size_t>(size) - 1);
    const cfg::ImportResult result = MakeLegacyImport().run(cfg::LegacyInput{missing, ansi, false}, mapped);
    Check(result.status == cfg::ImportStatus::Absent && result.dropped.empty(), "the old defaults drop nothing");
    Check(result.pose_shaping.size() == 6, "every sensitivity is recorded");
    for (const cfg::PoseShapingValue& value : result.pose_shaping) {
        Check(value.folded && value.shipped == "1.0",
              "[" + value.section + "] " + value.key + " shipped at identity and is folded");
    }
    Check(cfg::RenderCanonical(table, mapped, {kConfigDisplayName}) == Rendered(), "the old defaults map to the defaults");
    Check(mapped.toggle_key_name == "End, Ctrl+Shift+Y" && mapped.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G" &&
              mapped.yaw_mode_key_name == "PageDown, Ctrl+Shift+H",
          "the old hotkeys and the chords the builds always registered become the fleet's key lists");
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bytes.size()) {
        const size_t end = bytes.find("\r\n", start);
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many lines.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"a line was added or removed"};
    std::vector<std::string> changed;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

// A save changes the lines of its rows and no other byte, the yaw mode and the tracking mode
// persist, and End's row cannot be saved at all.
void TestTogglesSave() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"starfield-config-save-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring path = dir + L"\\" + Widen(kConfigFileName);
    const std::string committed = ReadBytes(Widen(SF_COMMITTED_CONFIG));
    WriteBytes(path, committed);

    {
        cfg::ConfigOwner<Config> owner(MakeConfigOwnerOptions(path));
        Check(owner.Load().status == cfg::ConfigLoadStatus::Canonical, "the committed file loads as canonical");

        Check(owner.Save([](Config& c) { c.world_space_yaw = false; }).status == cfg::ConfigSaveStatus::Saved,
              "the yaw mode saves");
        const std::string afterYaw = ReadBytes(path);
        Check(ChangedLines(committed, afterYaw) == std::vector<std::string>{"WorldSpaceYaw=false"},
              "saving the yaw mode changes its line and nothing else");

        const auto rotationOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
        Check(owner.Save([rotationOnly](Config& c) {
                  c.rotation_enabled = rotationOnly.rotation_enabled;
                  c.position_enabled = rotationOnly.position_enabled;
              }).status == cfg::ConfigSaveStatus::Saved,
              "the tracking mode saves");
        const std::string afterRotationOnly = ReadBytes(path);
        Check(ChangedLines(afterYaw, afterRotationOnly) == std::vector<std::string>{"PositionEnabled=false"},
              "saving rotation only changes PositionEnabled and nothing else");

        const auto positionOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::PositionOnly);
        Check(owner.Save([positionOnly](Config& c) {
                  c.rotation_enabled = positionOnly.rotation_enabled;
                  c.position_enabled = positionOnly.position_enabled;
              }).status == cfg::ConfigSaveStatus::Saved,
              "the third tracking mode saves");
        Check(ChangedLines(afterRotationOnly, ReadBytes(path)) ==
                  std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"},
              "saving position only changes the mode pair and nothing else");

        bool refused = false;
        try {
            owner.Save([](Config& c) { c.enable_on_startup = false; });
        } catch (const std::logic_error&) {
            refused = true;
        }
        Check(refused, "EnableOnStartup is not Writable, so the End toggle cannot persist");
    }

    cfg::ConfigOwner<Config> reopened(MakeConfigOwnerOptions(path));
    const auto again = reopened.Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical && again.diagnostics.empty() &&
              !again.config.world_space_yaw && !again.config.rotation_enabled && again.config.position_enabled &&
              again.config.enable_on_startup,
          "the saved yaw and tracking mode come back at the next start");

    DeleteFileW(path.c_str());
    RemoveDirectoryW(dir.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            const std::string rendered = Rendered();
            std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
            out.write(rendered.data(), static_cast<std::streamsize>(rendered.size()));
            if (!out) {
                std::printf("could not write %s\n", argv[2]);
                return 1;
            }
            return 0;
        }

        TestCommittedConfigIsRendered();
        TestLegacyDefaultsMapToTheDefaults();
        TestTogglesSave();
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config tests: all passed\n");
        return 0;
    }
    std::printf("config tests: %d failure(s)\n", g_failures);
    return 1;
}
