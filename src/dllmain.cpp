#include "pch.h"
#include "core/mod.h"
#include "core/logger.h"
#include <process.h>

static HANDLE g_initThreadHandle = nullptr;

namespace {

// The loader attaches through an import that resolves before the executable
// itself is mapped, so the game module is waited for rather than assumed.
constexpr int   kGameModuleAttempts = 100;
constexpr DWORD kGameModuleDelayMs  = 100;

// Long enough for the engine to build its singletons before anything is hooked.
constexpr DWORD kGameStartupDelayMs = 2000;

bool WaitForGameModule() {
    for (int attempt = 0; attempt < kGameModuleAttempts; ++attempt) {
        if (GetModuleHandleA(StarfieldHT::GAME_EXE)) return true;
        Sleep(kGameModuleDelayMs);
    }
    return false;
}

// The log is the only channel the mod has, so a log that cannot be opened has
// to be reported somewhere else or the whole session is silent and reads
// exactly like an ASI that never loaded. The game directory is reached through
// a WindowsApps junction and is not always writable.
void StartLogging() {
    if (StarfieldHT::Logger::Instance().Initialize()) return;
    OutputDebugStringA("StarfieldHeadTracking: could not open HeadTracking.log next to the game "
                       "executable - the mod runs but records nothing.\n");
}

} // namespace

unsigned __stdcall InitThread(void* lpParam) {
    (void)lpParam;

    if (!WaitForGameModule()) {
        StartLogging();
        StarfieldHT::Logger::Instance().Error(
            "%s was not loaded after %d ms - this process is not Starfield, or the "
            "executable has been renamed. Head tracking will not start.",
            StarfieldHT::GAME_EXE, kGameModuleAttempts * static_cast<int>(kGameModuleDelayMs));
        return 1;
    }

    StartLogging();
    StarfieldHT::Logger::Instance().Info("Starfield Head Tracking v%s attached to game process", StarfieldHT::VERSION);

    Sleep(kGameStartupDelayMs);

    // Initialize logs its own reason on every path it refuses on, dormancy on
    // an unrecognised build included, so there is nothing to add here.
    if (!StarfieldHT::Mod::Instance().Initialize()) return 1;

    StarfieldHT::Logger::Instance().Info("Starfield Head Tracking v%s loaded successfully", StarfieldHT::VERSION);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)lpReserved;

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            g_initThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr);
            if (!g_initThreadHandle) {
                OutputDebugStringA("StarfieldHeadTracking: could not start the init thread - "
                                   "the mod is inert for this session.\n");
            }
            break;

        case DLL_PROCESS_DETACH:
            // We're holding the loader lock here. Joining threads (init thread,
            // input polling thread) or running MinHook teardown under the lock
            // can deadlock if any of them touches LoadLibrary/GetModuleHandle,
            // and is pointless on process teardown because the OS will reclaim
            // everything. Only close the init-thread handle, which does not
            // block. The log is not closed either: every line is flushed as it
            // is written, and by the time this runs Windows has already killed
            // the threads that log, so one of them can be holding the logger's
            // mutex and never releasing it.
            if (g_initThreadHandle) {
                CloseHandle(g_initThreadHandle);
                g_initThreadHandle = nullptr;
            }
            break;
    }
    return TRUE;
}
