#pragma once

#include <cstdint>

namespace StarfieldHT {

// Gates head tracking to active gameplay.
//
// Two independent tests, both of which have to pass:
//
//   - The mouse capture. The game hides and clips the OS cursor while the
//     player is playing, and a menu that hands the pointer back shows it again.
//     Reading that rather than an engine singleton keeps the gate free of
//     per-build addresses. The foreground-window half of it is what stops
//     another application's capture from reading as gameplay, and rules out an
//     alt-tabbed session.
//
//   - The camera state the game is running. TESCamera keeps the active state at
//     +0x10 and each state is its own class, so the class name behind its vtable
//     says what the camera is doing: photo mode, the workshop's overhead view
//     and the console's free camera suppress tracking. Conversations permit
//     tracking with the same live FOV compensation as gameplay. States not on
//     the deny list are treated as gameplay, and the first sighting of each is
//     logged, so the list can be checked against what the game actually enters.
class GameState {
public:
    static void Initialize();
    static bool IsInGameplay();

    // Called from the camera hook with TESCamera's current state pointer.
    static void SetCameraState(uintptr_t state);
};

} // namespace StarfieldHT
