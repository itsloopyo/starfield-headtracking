// Dev builds only: the game takes its keyboard and mouse from a command file
// instead of the real devices, so a test session can run in the background.
#include "pch.h"

#include "core/logger.h"
#include "core/path_utils.h"

#define CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION
#include <cameraunlock/dev/isolated_input.h>

namespace StarfieldHT {

namespace {

void InputLog(const char* text) {
    Logger::Instance().Info("%s", text);
}

} // namespace

// Switched on by the command file being there, next to the mod, when the game
// starts. Without it a dev build answers to the real keyboard like any other.
void StartIsolatedInputIfAsked() {
    const std::wstring commandFile = GetModuleDirectoryW() + L"CameraUnlockInput.txt";
    if (GetFileAttributesW(commandFile.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (!cameraunlock::dev::StartIsolatedInput(commandFile, &InputLog)) {
        Logger::Instance().Error("Isolated input was asked for and could not be installed");
    }
}

} // namespace StarfieldHT
