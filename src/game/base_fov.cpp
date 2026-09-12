#include "pch.h"
#include "base_fov.h"

#include "core/logger.h"
#include "game/build_profile.h"

#include <cameraunlock/camera/zoom_compensation.h>
#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

namespace StarfieldHT {

namespace {

using cameraunlock::memory::SafeRead;

// The engine holds the vertical half-angle and widens the horizontal one to fill
// the display, and fFPWorldFOV is the horizontal angle at 16:9. Measured with
// the setting on 85: at aspect 1.778 the frustum reads r=0.9163 t=0.5154, and at
// 1.817 it reads r=0.9365 t=0.5154. So the vertical tangent is the one that can
// be compared against a setting, and tan(85/2) / (16/9) is 0.5154.
constexpr float kReferenceAspect = 16.0f / 9.0f;

// A Setting is {vtable, value, default, name}: the live value at +0x08 and a
// pointer to the name at +0x18. The name is read back before the value is, so a
// patch that moves the object leaves the mod reading nothing rather than reading
// whatever now sits at that address.
constexpr uintptr_t kSettingValueOffset = 0x08;
constexpr uintptr_t kSettingNameOffset  = 0x18;
constexpr char kBaseFovSetting[] = "fFPWorldFOV:Camera";

// The slider's own bounds are fFPWorldFOVMin/Max:Display. These are wider than
// either, and only reject a value that cannot be a field of view at all.
constexpr float kMinPlausibleFov = 30.0f;
constexpr float kMaxPlausibleFov = 170.0f;

uintptr_t g_setting = 0;

bool NameMatches(uintptr_t stringAddress, uintptr_t moduleBase, uintptr_t moduleEnd) {
    // The whole comparison has to fit inside the module, not just its first
    // byte: a name pointer landing on the last few bytes of the image would
    // otherwise have the compare read past the end of it.
    if (stringAddress < moduleBase) return false;
    if (stringAddress > moduleEnd - sizeof(kBaseFovSetting)) return false;
    __try {
        return memcmp(reinterpret_cast<const char*>(stringAddress), kBaseFovSetting,
                      sizeof(kBaseFovSetting)) == 0;
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        return false;
    }
}

// Deferred until the value is wanted rather than done at startup: the mod is
// loaded through the winmm import, which resolves before the executable's own
// static initialisers run, so the Setting is still zeroed while Mod::Initialize
// is running.
bool BindSetting() {
    if (g_setting != 0) return true;

    const BuildProfile* profile = ResolveBuildProfile();
    if (profile == nullptr) return false;

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t moduleBase = 0;
    size_t moduleSize = 0;
    if (!gameModule) return false;
    if (!cameraunlock::memory::GetModuleRange(gameModule, moduleBase, moduleSize)) return false;
    if (profile->baseFovSettingRva >= moduleSize) return false;

    const uintptr_t candidate = moduleBase + profile->baseFovSettingRva;
    uintptr_t namePointer = 0;
    if (!SafeRead(candidate + kSettingNameOffset, namePointer)) return false;
    if (!NameMatches(namePointer, moduleBase, moduleBase + moduleSize)) return false;

    g_setting = candidate;
    return true;
}

// Re-read every call rather than cached: the FOV slider is live, and a player
// who moves it mid-session must be measured against the value they moved it to
// without restarting the game.
float BaseTanHalfVertical() {
    if (!BindSetting()) return 0.0f;
    float degrees = 0.0f;
    if (!SafeRead(g_setting + kSettingValueOffset, degrees)) return 0.0f;
    if (!(degrees >= kMinPlausibleFov && degrees <= kMaxPlausibleFov)) return 0.0f;
    return tanf(0.5f * degrees * DEG_TO_RAD) / kReferenceAspect;
}

// A factor that is wrong by a constant reads exactly like a factor that is
// right, so every term of it goes in the log once, on the first camera update
// rather than the first pose: the basis is then readable without a tracker
// connected. The line has to say 1.0000 standing still, and anything else means
// the two tangents are not both vertical.
//
// Only a line that actually carries those terms latches. The setting binds
// through a pointer the game writes during its own start-up and the first
// camera update can beat it, so latching on the first CALL recorded "could not
// be read" for the whole session on a build where compensation went on to work
// from frame two - and that line is the release gate, so a gate that can latch
// the wrong answer is not one.
void ReportOnce(float baseTop, float frustumRight, float frustumTop, float factor) {
    static bool s_reported = false;
    if (s_reported) return;

    if (baseTop <= 0.0f) {
        static std::atomic<uint64_t> s_failures{0};
        const uint64_t n = s_failures.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((n & (n - 1)) != 0) return;
        Logger::Instance().Warning(
            "Zoom compensation is off: the game's own first-person FOV setting could not be "
            "read on this build, so the head pose is applied unscaled and tracking will feel "
            "stronger while the sights are up (total=%llu)",
            static_cast<unsigned long long>(n));
        return;
    }
    if (!(frustumTop > 0.0f)) return;
    s_reported = true;

    float degrees = 0.0f;
    SafeRead(g_setting + kSettingValueOffset, degrees);
    Logger::Instance().Info(
        "Zoom compensation: base FOV %.2f deg horizontal at 16:9 is tan(VFOV/2) %.4f, "
        "live frustum r=%.4f t=%.4f (aspect %.3f), factor %.4f",
        degrees, baseTop, frustumRight, frustumTop,
        frustumTop > 0.0f ? frustumRight / frustumTop : 0.0f, factor);
}

} // namespace

float PoseZoomFactor(float frustumRight, float frustumTop) {
    const float baseTop = BaseTanHalfVertical();
    if (baseTop <= 0.0f || !(frustumTop > 0.0f)) {
        ReportOnce(baseTop, frustumRight, frustumTop, 1.0f);
        return 1.0f;
    }

    const float factor = cameraunlock::camera::FovZoomFactor(frustumTop, baseTop);
    ReportOnce(baseTop, frustumRight, frustumTop, factor);
    return factor;
}

} // namespace StarfieldHT
