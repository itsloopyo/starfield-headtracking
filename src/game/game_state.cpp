#include "pch.h"
#include "game_state.h"
#include "core/logger.h"
#include "core/rtti_utils.h"

#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {

namespace {

// True when the cursor is confined to something smaller than the whole desktop.
// Windows reports an unclipped cursor as the full virtual screen, so anything
// narrower means an application has taken the mouse.
bool CursorIsClipped() {
    RECT clip{};
    if (!GetClipCursor(&clip)) return false;
    const long vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const long vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const long vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const long vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return !(clip.left <= vx && clip.top <= vy &&
             clip.right >= vx + vw && clip.bottom >= vy + vh);
}

// Either half of the mouse capture is enough to mean "playing". Requiring both
// would drop tracking the moment any overlay in the session shows a cursor,
// because cursor visibility is global desktop state rather than per-window;
// requiring only visibility would miss a state that shows a cursor without
// releasing the clip.
bool MouseSaysPlaying() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) return false;

    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) {
        static bool s_failLogged = false;
        if (!s_failLogged) {
            s_failLogged = true;
            Logger::Instance().Warning(
                "GetCursorInfo failed (%lu) - falling back to the cursor clip alone to tell "
                "gameplay from a menu", GetLastError());
        }
        return CursorIsClipped();
    }
    return ((ci.flags & CURSOR_SHOWING) == 0) || CursorIsClipped();
}

// Camera states the player is not playing through. Matched as substrings of the
// decorated RTTI name, so ".?AVPhotoModeCameraState@@" is caught by "PhotoMode".
// Anything not listed counts as gameplay: the list names the cases where moving
// the camera is wrong, and a state nobody has seen yet is far more likely to be
// another way of walking around than another cutscene.
const char* const kNonGameplayStates[] = {
    "PhotoMode",
    "TweenMenu",
    "WorkshopIso",
    "VATS",
    "FreeFly",
    "FreeAdvanced",
    "FreeTethered",
    "ShipFarTravel",
};

bool NameIsNonGameplay(const char* name) {
    for (const char* needle : kNonGameplayStates) {
        if (strstr(name, needle) != nullptr) return true;
    }
    return false;
}

// One entry per camera-state vtable seen this session. The lookup runs on the
// render path, and resolving a class name walks three pointers of RTTI, so the
// answer is cached against the vtable rather than recomputed.
struct StateEntry {
    uintptr_t vtable;
    bool      gameplay;
};
constexpr int kMaxStates = 32;
StateEntry g_states[kMaxStates] = {};
int        g_stateCount = 0;

std::atomic<bool> g_cameraStateIsGameplay{true};

bool ClassifyState(uintptr_t vtable) {
    for (int i = 0; i < g_stateCount; ++i) {
        if (g_states[i].vtable == vtable) return g_states[i].gameplay;
    }

    // Nothing past the table is resolved or logged. Resolving costs a walk of
    // three levels of RTTI on the camera update path and the log line is one a
    // frame, so a full table used to turn every further unknown state into
    // per-frame RTTI plus sixty lines a second, forever.
    if (g_stateCount >= kMaxStates) {
        static bool s_overflowLogged = false;
        if (!s_overflowLogged) {
            s_overflowLogged = true;
            Logger::Instance().Warning(
                "More than %d camera states seen this session - further unrecognised states "
                "count as gameplay without being named", kMaxStates);
        }
        return true;
    }

    char name[192] = {};
    bool gameplay = true;
    if (GetClassNameFromVtable(vtable, name, sizeof(name))) {
        gameplay = !NameIsNonGameplay(name);
        Logger::Instance().Info("Camera state %s -> %s", name,
                                gameplay ? "gameplay" : "tracking suppressed");
    } else {
        Logger::Instance().Warning(
            "Camera state at vtable 0x%llX has no readable RTTI - treating as gameplay",
            static_cast<unsigned long long>(vtable));
    }

    g_states[g_stateCount].vtable = vtable;
    g_states[g_stateCount].gameplay = gameplay;
    ++g_stateCount;
    return gameplay;
}

} // namespace

void GameState::Initialize() {
    Logger::Instance().Info("Game state gate: cursor capture + foreground window + camera state");
}

void GameState::SetCameraState(uintptr_t state) {
    if (state == 0) {
        g_cameraStateIsGameplay.store(false, std::memory_order_relaxed);
        return;
    }
    uintptr_t vtable = 0;
    if (!cameraunlock::memory::SafeRead(state, vtable) || vtable == 0) {
        g_cameraStateIsGameplay.store(false, std::memory_order_relaxed);
        return;
    }
    g_cameraStateIsGameplay.store(ClassifyState(vtable), std::memory_order_relaxed);
}

bool GameState::IsInGameplay() {
    if (!g_cameraStateIsGameplay.load(std::memory_order_relaxed)) return false;

    // The mouse half is polled rather than read every frame: it is three OS
    // calls, and the answer cannot change between two frames in any way a
    // player would notice. Atomic because the simulation thread and the HUD
    // thread both ask, and a stale poll from the other one is harmless while a
    // torn one is not.
    constexpr ULONGLONG kIntervalMs = 100;
    static std::atomic<ULONGLONG> s_lastCheckMs{0};
    static std::atomic<bool>      s_lastResult{false};

    const ULONGLONG now = GetTickCount64();
    if (now - s_lastCheckMs.load(std::memory_order_relaxed) >= kIntervalMs) {
        s_lastCheckMs.store(now, std::memory_order_relaxed);
        s_lastResult.store(MouseSaysPlaying(), std::memory_order_relaxed);
    }
    return s_lastResult.load(std::memory_order_relaxed);
}

} // namespace StarfieldHT
