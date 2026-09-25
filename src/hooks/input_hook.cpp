#include "pch.h"
#include "input_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#if STARFIELDHT_DEV_HOTKEYS
#include "core/access_probe.h"
#include "core/object_finder.h"
#include "game/hud_probe.h"
#include <cameraunlock/input/chord_hotkeys.h>
#endif
#include "hooks/camera_hook.h"

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/key_binding_registration.h>
#include <cameraunlock/input/key_bindings.h>

#include <stdexcept>
#include <string>
#include <vector>

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

// The config table already refused a list that does not parse, so one here is
// a bug rather than a player's typo.
std::vector<cameraunlock::input::KeyBinding> Parse(const std::string& list) {
    cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error("hotkey list '" + list + "' does not parse: " + parsed.error);
    return parsed.bindings;
}

void RegisterBindings(const Config& config) {
    // Each list from HeadTracking.ini, chords included. A plain key does not
    // fire while Ctrl and Shift are both held, so Ctrl+Shift with a key reaches
    // only a binding that names the chord.
    using cameraunlock::input::RegisterKeyBindings;
    RegisterKeyBindings(g_poller, Parse(config.toggle_key_name), [] { Mod::Instance().Toggle(); });
    RegisterKeyBindings(g_poller, Parse(config.cycle_tracking_mode_key_name), [] { Mod::Instance().CycleDofMode(); });
    RegisterKeyBindings(g_poller, Parse(config.yaw_mode_key_name), [] { Mod::Instance().ToggleYawMode(); });

#if STARFIELDHT_DEV_HOTKEYS
    using cameraunlock::input::NavGuarded;
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

    Logger::Instance().Info("Input hook installed - Toggle: [%s]", config.toggle_key_name.c_str());

    return true;
}

} // namespace StarfieldHT
