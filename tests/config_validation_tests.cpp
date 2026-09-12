// Behaviour lock for the config boundary: an INI the user has hand-edited
// becoming values the rest of the mod trusts.
//
// The hotkey half is the one with a bug behind it. Hotkey codes are parsed with
// strtol on base 0, so anything at all reaches the poller: a negative number, a
// decimal typed where hex was meant, a scancode copied from the wrong table.
// GetAsyncKeyState only defines codes 0x01 to 0xFE, so every one of those
// silently costs the user the binding with nothing in the log to say why.
//
// The parsing half exists because atoi and atof answered every one of those
// with a plausible-looking number instead: "yes" read as false, and "0,4" - what
// a comma-decimal keyboard produces - read as zero and then clamped to a
// one-centimetre lean that looks exactly like broken tracking.

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <sstream>

#include "core/config.h"
#include "core/constants.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void CheckEqual(int actual, int expected, const char* what) {
    if (actual == expected) return;
    std::printf("FAIL: %s (expected 0x%X, got 0x%X)\n", what, expected, actual);
    ++g_failures;
}

void CheckNear(float actual, float expected, const char* what) {
    const float d = actual - expected;
    if ((d < 0 ? -d : d) <= 1e-4f) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what, expected, actual);
    ++g_failures;
}

void OutOfRangeHotkeysFallBackToDefaults() {
    // 0 is what an empty or unparsable value produces, -1 what a bare "-1"
    // does, and 0x101 what a code past the end of the virtual key table looks
    // like. None of them can ever fire.
    StarfieldHT::Config config;
    config.toggleKey = 0;
    config.positionToggleKey = -1;
    config.yawModeKey = 0xFF;
    config.adsModeKey = 0x101;
    config.Validate();

    CheckEqual(config.toggleKey, StarfieldHT::DEFAULT_TOGGLE_KEY,
               "a zero toggle key falls back to the default");
    CheckEqual(config.positionToggleKey, StarfieldHT::DEFAULT_POSITION_TOGGLE_KEY,
               "a negative mode key falls back to the default");
    CheckEqual(config.yawModeKey, StarfieldHT::DEFAULT_YAW_MODE_KEY,
               "0xFF is past the end of the virtual key table");
    CheckEqual(config.adsModeKey, StarfieldHT::DEFAULT_ADS_MODE_KEY,
               "a key code wider than a byte falls back to the default");
}

void ValidHotkeysSurviveValidation() {
    StarfieldHT::Config config;
    // 0x01 is the bottom of the range GetAsyncKeyState defines, not a binding
    // anybody should choose; what is locked here is that the floor does not
    // drift upward and start rejecting codes that do fire.
    config.toggleKey = 0x01;
    config.positionToggleKey = 0x79; // VK_F10
    config.yawModeKey = 0xFE;        // the top of the range
    config.adsModeKey = 'K';
    config.Validate();

    CheckEqual(config.toggleKey, 0x01, "0x01 is the bottom of the virtual key range");
    CheckEqual(config.positionToggleKey, 0x79, "a function key is left alone");
    CheckEqual(config.yawModeKey, 0xFE, "0xFE is a valid virtual key code");
    CheckEqual(config.adsModeKey, 'K', "a letter key is left alone");
}

void DefaultsAreThemselvesValid() {
    StarfieldHT::Config config;
    config.SetDefaults();
    const StarfieldHT::Config before = config;
    config.Validate();

    CheckEqual(config.toggleKey, before.toggleKey, "validation does not move the default toggle key");
    CheckEqual(config.positionToggleKey, before.positionToggleKey, "validation does not move the default mode key");
    CheckEqual(config.yawModeKey, before.yawModeKey, "validation does not move the default yaw key");
    CheckEqual(config.adsModeKey, before.adsModeKey, "validation does not move the default sights key");
    Check(config.udpPort == before.udpPort, "validation does not move the default UDP port");
    CheckNear(config.localSmoothing, before.localSmoothing, "validation does not move local smoothing");
    CheckNear(config.remoteSmoothing, before.remoteSmoothing, "validation does not move remote smoothing");
    CheckNear(config.positionLimitZ, before.positionLimitZ, "validation does not move the forward lean limit");
}

// Every range is the one the shared configuration table specifies, so a value
// means the same thing here as in every other mod in the fleet. The position
// limits are metres of eye travel with nothing sweeping the level geometry yet,
// so the ceiling is what keeps a lean inside the room the player is standing in.
void NumericSettingsAreClamped() {
    StarfieldHT::Config config;
    config.yawMultiplier = 99.0f;
    config.pitchMultiplier = -4.0f;
    config.rollMultiplier = 99.0f;
    config.localSmoothing = 5.0f;
    config.remoteSmoothing = -1.0f;
    config.positionSensitivityX = 99.0f;
    config.positionLimitX = 9.0f;
    config.positionLimitY = 9.0f;
    config.positionLimitZ = 9.0f;
    config.positionLimitZBack = 9.0f;
    config.Validate();

    CheckNear(config.yawMultiplier, 3.0f, "yaw sensitivity is clamped to its ceiling");
    CheckNear(config.pitchMultiplier, 0.1f, "pitch sensitivity is clamped to its floor");
    CheckNear(config.rollMultiplier, 3.0f, "roll sensitivity is clamped to its ceiling");
    CheckNear(config.localSmoothing, 1.0f, "local smoothing is clamped to 1.0");
    CheckNear(config.remoteSmoothing, 0.0f, "remote smoothing is clamped to 0.0");
    CheckNear(config.positionSensitivityX, 5.0f, "position sensitivity is clamped to its ceiling");
    CheckNear(config.positionLimitX, 0.5f, "the lateral lean limit is clamped to 0.5m");
    CheckNear(config.positionLimitY, 0.5f, "the vertical lean limit is clamped to 0.5m");
    CheckNear(config.positionLimitZ, 0.5f, "the forward lean limit is clamped to 0.5m");
    CheckNear(config.positionLimitZBack, 0.5f, "the backward lean limit is clamped to 0.5m");
}

std::string TempPath(const char* leaf) {
    std::string path = "starfieldht_config_test_";
    path += leaf;
    return path;
}

bool WriteFile(const std::string& path, const std::string& body) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return false;
    out << body;
    return true;
}

// Everything above drives Validate() on fields assigned directly, which is the
// half of the boundary that never touches the parser. These drive the parser.
void FileValuesReachTheStruct() {
    const std::string path = TempPath("good.ini");
    if (!WriteFile(path,
                   "[Network]\nUDPPort=5510\n"
                   "[Sensitivity]\nLocalSmoothing=0.25\n"
                   "[Position]\nLimitZ=0.35\nEnabled=false\n"
                   "[General]\nWorldSpaceYaw=false\n"
                   "[Crosshair]\nShow=false\n"
                   "[Ship]\nAimUIFollowsHead=true\n")) {
        Check(false, "could not write the parser fixture");
        return;
    }

    StarfieldHT::Config config;
    Check(config.Load(path.c_str()), "a well-formed file loads");
    Check(config.udpPort == 5510, "the port comes from the file");
    CheckNear(config.localSmoothing, 0.25f, "local smoothing comes from the file");
    CheckNear(config.positionLimitZ, 0.35f, "the forward lean limit comes from the file");
    Check(!config.positionEnabled, "position tracking can be switched off from the file");
    Check(!config.worldSpaceYaw, "the yaw mode comes from the file");
    Check(!config.showCrosshair, "the crosshair setting comes from the file");
    Check(config.shipAimUIFollowsHead, "the ship aim UI can follow the head");
    Check(config.Save(path.c_str()), "the selected ship aim UI mode saves");
    StarfieldHT::Config reloaded;
    Check(reloaded.Load(path.c_str()), "the saved configuration reloads");
    Check(reloaded.shipAimUIFollowsHead, "the selected ship aim UI mode survives a round trip");
    std::remove(path.c_str());
}

// A value the parser cannot read keeps the previous one and says so, rather
// than folding into a number that looks deliberate.
void UnreadableValuesKeepThePreviousOne() {
    const std::string path = TempPath("bad.ini");
    if (!WriteFile(path,
                   "[Position]\nLimitZ=0,35\nLimitX=\nEnabled=maybe\n"
                   "[Network]\nUDPPort=70000\n")) {
        Check(false, "could not write the malformed fixture");
        return;
    }

    const StarfieldHT::Config defaults;
    StarfieldHT::Config config;
    Check(config.Load(path.c_str()), "a file with unreadable values still loads");
    CheckNear(config.positionLimitZ, defaults.positionLimitZ,
              "a comma decimal separator does not become a one-centimetre lean");
    CheckNear(config.positionLimitX, defaults.positionLimitX,
              "an empty value keeps the previous one");
    Check(config.positionEnabled == defaults.positionEnabled,
          "a word that is neither true nor false keeps the previous value");
    Check(config.udpPort == defaults.udpPort, "a port past the end of the range keeps the default");
    std::remove(path.c_str());
}

// "yes" and "on" are what people type, and atoi read both as false.
void CommonWordsForYesAndNoAreUnderstood() {
    const std::string path = TempPath("words.ini");
    if (!WriteFile(path, "[Position]\nEnabled=yes\n[General]\nAutoEnable=OFF\n")) {
        Check(false, "could not write the word fixture");
        return;
    }

    StarfieldHT::Config config;
    Check(config.Load(path.c_str()), "the word fixture loads");
    Check(config.positionEnabled, "\"yes\" reads as true");
    Check(!config.autoEnable, "\"OFF\" reads as false");
    std::remove(path.c_str());
}

std::set<std::string> KeysIn(std::istream& in) {
    std::set<std::string> keys;
    std::string line, section;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        if (line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            const size_t close = line.find(']');
            if (close != std::string::npos) section = line.substr(1, close - 1);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        keys.insert(section + "/" + name);
    }
    return keys;
}

// The shipped HeadTracking.ini is what an installer and the launcher's manifest
// seed both put next to the game, and the mod only writes its own when the file
// is absent - so a key the writer knows about but the shipped file lacks is a
// key no installed user can ever discover. That is exactly how [Crosshair] Show
// came to be readable, documented and missing from every install.
void ShippedIniCarriesEveryKeyTheWriterEmits() {
    const std::string generatedPath = TempPath("generated.ini");
    const StarfieldHT::Config defaults;
    if (!defaults.Save(generatedPath.c_str())) {
        Check(false, "could not write the generated config");
        return;
    }

    std::ifstream generated(generatedPath);
    std::ifstream shipped(STARFIELDHT_SHIPPED_INI);
    if (!generated.is_open() || !shipped.is_open()) {
        Check(false, "could not read both the generated and the shipped config");
        std::remove(generatedPath.c_str());
        return;
    }
    const std::set<std::string> generatedKeys = KeysIn(generated);
    const std::set<std::string> shippedKeys = KeysIn(shipped);
    // Windows refuses to remove a file a stream still holds open.
    generated.close();
    shipped.close();

    for (const std::string& key : generatedKeys) {
        if (shippedKeys.count(key) != 0) continue;
        std::printf("FAIL: HeadTracking.ini is missing [%s], which Config::Save writes\n", key.c_str());
        ++g_failures;
    }
    for (const std::string& key : shippedKeys) {
        if (generatedKeys.count(key) != 0) continue;
        std::printf("FAIL: HeadTracking.ini carries [%s], which nothing reads back\n", key.c_str());
        ++g_failures;
    }
    std::remove(generatedPath.c_str());

    // And the values in it are the defaults, so a user who deletes the file
    // gets back what they were shipped.
    StarfieldHT::Config loaded;
    Check(loaded.Load(STARFIELDHT_SHIPPED_INI), "the shipped config parses");
    Check(loaded.udpPort == defaults.udpPort, "the shipped port is the default");
    CheckNear(loaded.localSmoothing, defaults.localSmoothing, "the shipped local smoothing is the default");
    CheckNear(loaded.remoteSmoothing, defaults.remoteSmoothing, "the shipped remote smoothing is the default");
    CheckNear(loaded.positionLimitZ, defaults.positionLimitZ, "the shipped forward lean limit is the default");
    CheckEqual(loaded.toggleKey, defaults.toggleKey, "the shipped toggle key is the default");
    CheckEqual(loaded.adsModeKey, defaults.adsModeKey, "the shipped sights key is the default");
    Check(loaded.showCrosshair == defaults.showCrosshair, "the shipped crosshair setting is the default");
    Check(!defaults.shipAimUIFollowsHead && !loaded.shipAimUIFollowsHead,
          "the shipped and generated ship aim UI defaults anchor to the forward view");
}

} // namespace

int main() {
    OutOfRangeHotkeysFallBackToDefaults();
    ValidHotkeysSurviveValidation();
    DefaultsAreThemselvesValid();
    NumericSettingsAreClamped();
    FileValuesReachTheStruct();
    UnreadableValuesKeepThePreviousOne();
    CommonWordsForYesAndNoAreUnderstood();
    ShippedIniCarriesEveryKeyTheWriterEmits();

    if (g_failures != 0) {
        std::printf("%d config validation check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("config validation: all checks passed\n");
    return 0;
}
