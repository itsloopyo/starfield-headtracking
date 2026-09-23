#include "pch.h"
#include "input_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/hotkey_utils.h"
#if STARFIELDHT_DEV_HOTKEYS
#include "core/access_probe.h"
#include "core/object_finder.h"
#include "game/hud_probe.h"
#endif
#include "hooks/camera_hook.h"

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/chord_hotkeys.h>

// Extra logging hotkeys, off in a shipped build and not something a player
// needs. Configure with -DSTARFIELDHT_DEV_HOTKEYS=ON to re-arm them.
#ifndef STARFIELDHT_DEV_HOTKEYS
#define STARFIELDHT_DEV_HOTKEYS 0
#endif

namespace StarfieldHT {

namespace {

// ~60Hz polling - gentle enough that GetAsyncKeyState never misses a quick tap.
constexpr int kPollIntervalMs = 16;

#if STARFIELDHT_DEV_HOTKEYS
// Rows 0..2 of the camera's world transform - its forward, up and right axes,
// and nothing else. The translation that follows them at +0xB0 is read by a
// great deal of ordinary game code and head rotation does not change it, so
// watching it as well buries the readers that matter.
constexpr uintptr_t kCameraBasisOffset = 0x80;
constexpr size_t    kCameraBasisBytes  = 0x30;
constexpr int       kAccessProbeHits   = 4000;
#endif

cameraunlock::input::HotkeyPoller g_poller;
std::atomic<bool> g_running{false};
bool g_bindingsRegistered = false;

void RegisterBindings(const Config& config) {
    using cameraunlock::input::NavGuarded;
    using cameraunlock::input::ChordGuarded;

    // Nav-cluster bindings (configurable via HeadTracking.ini). NavGuarded
    // suppresses them while Ctrl+Shift is held so a single keypress can't fire
    // two actions on layouts where a chord letter aliases a nav-cluster scancode.
    g_poller.AddHotkey(config.toggleKey,         NavGuarded([] { Mod::Instance().Toggle(); }));
    g_poller.AddHotkey(config.positionToggleKey, NavGuarded([] { Mod::Instance().CycleDofMode(); }));
    g_poller.AddHotkey(config.yawModeKey,        NavGuarded([] { Mod::Instance().ToggleYawMode(); }));

    // Ctrl+Shift+<letter> chord alternatives per the CameraUnlock standard:
    // Y=Toggle, G=Position, H=yaw mode.
    g_poller.AddHotkey('Y', ChordGuarded([] { Mod::Instance().Toggle(); }));
    g_poller.AddHotkey('G', ChordGuarded([] { Mod::Instance().CycleDofMode(); }));
    g_poller.AddHotkey('H', ChordGuarded([] { Mod::Instance().ToggleYawMode(); }));

#if STARFIELDHT_DEV_HOTKEYS
    // Diagnostics: F8 cycles axis isolation, F6 dumps camera matrices, Delete
    // records which game code reads the camera.
    // (access_probe.cpp is only compiled into a dev build.)
    g_poller.AddHotkey(VK_F8, NavGuarded([] { Mod::Instance().CycleAxisIsolation(); }));
    g_poller.AddHotkey(VK_F6, NavGuarded([] { Mod::Instance().DumpMatrices(); }));
    g_poller.AddHotkey(VK_F5, NavGuarded([] { ProbeHud(); }));
    // Home is deliberately unbound across the fleet, muscle memory from a
    // binding these mods no longer have, so the object dump sits on F7.
    g_poller.AddHotkey(VK_F7, NavGuarded([] {
        for (const char* name : {".?AVHUDMenu@@", ".?AVHUDCrosshairDataModel@@"}) {
            DumpInstancesByRtti(name, 3, 0x120);
        }
    }));
    g_poller.AddHotkey(VK_DELETE, NavGuarded([] {
        const uintptr_t camera = GetLiveCameraAddress();
        if (camera == 0) {
            Logger::Instance().Info("Access probe: no live camera yet");
            return;
        }
        ArmAccessProbe(camera + kCameraBasisOffset, kCameraBasisBytes, kAccessProbeHits);
    }));
#endif
}

} // namespace

bool InstallInputHook() {
    if (g_running.load()) {
        return true;
    }

    const Config& config = Mod::Instance().GetConfig();
    if (!g_bindingsRegistered) {
        RegisterBindings(config);
        g_bindingsRegistered = true;
    }

    if (!g_poller.Start(kPollIntervalMs)) {
        Logger::Instance().Error("Hotkey poller failed to start");
        return false;
    }
    g_running.store(true);

    Logger::Instance().Info("Input hook installed - Toggle: %s",
        VirtualKeyToString(config.toggleKey));

    return true;
}

} // namespace StarfieldHT
