#include "pch.h"
#include "stock_reticle.h"

#include "core/logger.h"
#include "core/mod.h"
#include "core/rtti_utils.h"
#include "game/build_profile.h"
#include "game/game_state.h"
#include "game/impact_projection.h"
#include "hooks/camera_hook.h"

#include <cameraunlock/memory/pattern_scanner.h>
#include <cameraunlock/memory/safe_memory.h>

#include <cstddef>
#include <cmath>

namespace StarfieldHT {
namespace {

struct GfxNumber {
    uintptr_t previous = 0;
    uintptr_t next = 0;
    uintptr_t objectInterface = 0;
    uint32_t type = 5;
    uint32_t padding = 0;
    double number = 0;
    uintptr_t auxiliary = 0;
};
static_assert(sizeof(GfxNumber) == 0x30);
static_assert(offsetof(GfxNumber, type) == 0x18);
static_assert(offsetof(GfxNumber, number) == 0x20);


struct MovieRect { float left, top, right, bottom; };

// GFxValue's type tag: 5 is a number, and bit 6 marks a managed value that owns
// a reference the caller has to release.
constexpr uint32_t kGfxTypeNumber  = 5;
constexpr uint32_t kGfxManagedFlag = 0x40;

// IMenu -> GFxMovieView -> the movie's root display object.
constexpr uintptr_t kMenuMovieOffset = 0x88;
constexpr uintptr_t kMovieRootOffset = 0x18;

// Vtable slots on the Scaleform objects the HUD is driven through.
constexpr size_t kSlotSetVariable   = 49;
constexpr size_t kSlotGetVariable   = 50;
constexpr size_t kSlotGetVisibleRect = 18;

// An aim behind the view has no screen position, so the reticle is parked this
// many frame widths outside the visible area. Anything past 1 is off screen;
// 2 leaves room for the sprite's own extent.
constexpr double kOffscreenFrameWidths = 2.0;

using HudUpdate = void (*)(uintptr_t);
HudUpdate g_original = nullptr;
void* g_target = nullptr;
void (*g_releaseValue)(GfxNumber*) = nullptr;

constexpr char kReticleX[] = "root1.CenterGroup_mc.ReticleBase_mc.x";
constexpr char kReticleY[] = "root1.CenterGroup_mc.ReticleBase_mc.y";

using HitEvent = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t);
HitEvent g_playerHitOriginal = nullptr;
HitEvent g_hudHitOriginal = nullptr;
// The player's damage callback emits the HUD event synchronously, but drops
// the impact position from that event. Keep it only for that call chain.
thread_local uintptr_t g_hitData = 0;
uintptr_t g_worldOriginIndex = 0;
using RelativeImpact = float* (*)(float*, const ImpactPoint*, const float*);
RelativeImpact g_relativeImpact = nullptr;
std::mutex g_impactMutex;
ImpactPoint g_impact;
bool g_hasImpact = false;

uintptr_t PlayerHitEvent(uintptr_t sink, uintptr_t data, uintptr_t source) {
    const uintptr_t previous = g_hitData;
    g_hitData = data;
    const auto result = g_playerHitOriginal(sink, data, source);
    g_hitData = previous;
    return result;
}

uintptr_t HudHitEvent(uintptr_t sink, uintptr_t data, uintptr_t source) {
    ImpactPoint impact;
    bool valid = false;
    if (g_hitData) {
        valid = cameraunlock::memory::SafeRead(g_hitData, impact.position)
             && cameraunlock::memory::SafeRead(g_worldOriginIndex, impact.originIndex)
             && std::isfinite(impact.position[0]) && std::isfinite(impact.position[1])
             && std::isfinite(impact.position[2]);
        if (!valid) Logger::Instance().Error("Hit marker: cannot read the impact position or world origin");
    }
    {
        const std::lock_guard<std::mutex> lock(g_impactMutex);
        g_impact = impact;
        g_hasImpact = valid;
    }
    return g_hudHitOriginal(sink, data, source);
}

// The vtable pointer and the slot are both read rather than dereferenced. A
// HUD torn down between frames leaves a plausible-looking address in the menu's
// movie member, and calling straight through slot 49 of whatever is now mapped
// there transfers control into recycled heap.
template<class F>
F Virtual(uintptr_t object, size_t slot) {
    uintptr_t vtable = 0;
    if (!cameraunlock::memory::SafeRead(object, vtable) || vtable == 0) return nullptr;
    uintptr_t entry = 0;
    if (!cameraunlock::memory::SafeRead(vtable + slot * sizeof(uintptr_t), entry) || entry == 0) {
        return nullptr;
    }
    return reinterpret_cast<F>(entry);
}

bool GetNumber(uintptr_t root, const char* path, double& out) {
    using GetVariable = bool (*)(uintptr_t, GfxNumber*, const char*);
    const GetVariable getVariable = Virtual<GetVariable>(root, kSlotGetVariable);
    if (!getVariable) return false;

    GfxNumber value;
    const bool found = getVariable(root, &value, path);
    if (value.type & kGfxManagedFlag) {
        g_releaseValue(&value);
        return false;
    }
    if (!found || value.type != kGfxTypeNumber || !std::isfinite(value.number)) return false;
    out = value.number;
    return true;
}

bool SetNumber(uintptr_t root, const char* path, double number) {
    using SetVariable = bool (*)(uintptr_t, const char*, const GfxNumber*, uint32_t);
    const SetVariable setVariable = Virtual<SetVariable>(root, kSlotSetVariable);
    if (!setVariable) return false;

    GfxNumber value;
    value.number = number;
    return setVariable(root, path, &value, 0);
}

// A frame the HUD could not be asked about. Retried next frame rather than
// latched, so the count is what has to be bounded.
void ReportTransientFailure(const char* what) {
    static std::atomic<uint64_t> s_count{0};
    const uint64_t n = s_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((n & (n - 1)) != 0) return;
    Logger::Instance().Warning("Stock reticle: %s (total=%llu)", what,
                               static_cast<unsigned long long>(n));
}

// Every term of the reticle placement on one line, once a second while it is
// being placed: the aim depth, the lean resolved along the clean camera's own
// axes, where the aim direction alone would land, and where the point lands.
// The parallax is the difference of the last two, so one line is enough to
// check it against lean / (distance - forward lean) by hand.
void LogReticleGeometry(const CameraFrame& frame, bool projected, float distance,
                        float ndcX, float ndcY) {
    static std::atomic<uint64_t> s_lastMs{0};
    const uint64_t now = GetTickCount64();
    if (now - s_lastMs.load(std::memory_order_relaxed) < 1000) return;
    s_lastMs.store(now, std::memory_order_relaxed);

    float lean[3];
    for (int i = 0; i < 3; ++i) lean[i] = frame.drawn.e[i] - frame.clean.e[i];
    float dirX = 0, dirY = 0;
    const bool direction = ProjectAimDirection(frame, dirX, dirY);
    Logger::Instance().Info(
        "reticle: dist=%.3f lean(r,u,f)=(%+.3f,%+.3f,%+.3f) direction=%s(%+.4f,%+.4f) "
        "point=%s(%+.4f,%+.4f) parallax=(%+.4f,%+.4f) eye(%.2f,%.2f,%.2f)",
        distance, Dot3(lean, frame.clean.r), Dot3(lean, frame.clean.u), Dot3(lean, frame.clean.f),
        direction ? "" : "none", dirX, dirY, projected ? "" : "none", ndcX, ndcY,
        ndcX - dirX, ndcY - dirY, frame.clean.e[0], frame.clean.e[1], frame.clean.e[2]);
}

// The reticle's own coordinates are in its parent's space, so the projected
// offset has to be divided back through every scale between the movie's visible
// frame and the reticle. False when the HUD cannot supply one of those numbers.
bool ReticleOffset(uintptr_t movie, uintptr_t root, const CameraFrame& frame,
                   double& outX, double& outY) {
    using GetRect = MovieRect* (*)(uintptr_t, MovieRect*);
    const GetRect getRect = Virtual<GetRect>(movie, kSlotGetVisibleRect);
    if (!getRect) return false;
    MovieRect rect{};
    getRect(movie, &rect);

    double rootX = 0, rootY = 0, groupX = 0, groupY = 0;
    if (!GetNumber(root, "root1.scaleX", rootX)
        || !GetNumber(root, "root1.scaleY", rootY)
        || !GetNumber(root, "root1.CenterGroup_mc.scaleX", groupX)
        || !GetNumber(root, "root1.CenterGroup_mc.scaleY", groupY)
        || rootX * groupX == 0 || rootY * groupY == 0
        || !(rect.right > rect.left) || !(rect.bottom > rect.top)) {
        return false;
    }

    const double width = rect.right - rect.left;
    const double height = rect.bottom - rect.top;
    float ndcX = 0, ndcY = 0, distance = 0;
    const bool projected = ProjectPlayerAim(frame, ndcX, ndcY, &distance);
    LogReticleGeometry(frame, projected, distance, ndcX, ndcY);
    if (projected) {
        outX = ndcX * width * 0.5 / (rootX * groupX);
        outY = -ndcY * height * 0.5 / (rootY * groupY);
    } else {
        // Keep the clip outside the frame without changing the game's own
        // visibility flag.
        outX = kOffscreenFrameWidths * width / (rootX * groupX);
        outY = 0.0;
    }
    return true;
}

// The HUD movie is a game object with a lifetime the mod does not control, and
// this runs every frame the HUD updates, so a repeated fault would otherwise
// write a line a frame. Doubling keeps the first, the fact that it is still
// happening, and a running total.
void ReportHudException(DWORD code, const char* action = "positioning the stock reticle") {
    static std::atomic<uint64_t> s_count{0};
    const uint64_t n = s_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((n & (n - 1)) != 0) return;
    Logger::Instance().Warning(
        "HUD access violation while %s (code=0x%08X, total=%llu)",
        action, code, static_cast<unsigned long long>(n));
}

void PositionReticle(uintptr_t menu) {
    // Read through SafeRead rather than dereferenced: the movie pointer is a
    // member of the menu the game handed us, but the root is read out of
    // whatever that member holds, and a HUD being torn down puts a freed
    // address there.
    uintptr_t movie = 0;
    if (!cameraunlock::memory::SafeRead(menu + kMenuMovieOffset, movie) || !movie) return;
    uintptr_t root = 0;
    if (!cameraunlock::memory::SafeRead(movie + kMovieRootOffset, root) || !root) return;

    // The origin is re-read whenever the HUD movie is rebuilt. Only a movie
    // that cannot supply one at all is given up on, and only for as long as it
    // is the current movie: a per-frame projection or write failure is
    // transient - a resolution change, an alt-tab out of exclusive fullscreen
    // and a HUD relayout all produce a degenerate visible rect or a zero
    // parent scale for a frame or two - and latching on one of those left the
    // reticle pinned at screen centre for the rest of the session.
    static uintptr_t lastMovie = 0;
    static double baseX = 0, baseY = 0;
    static bool unbindable = false;
    if (movie != lastMovie) {
        lastMovie = movie;
        unbindable = false;
        if (!GetNumber(root, kReticleX, baseX) || !GetNumber(root, kReticleY, baseY)) {
            Logger::Instance().Error("Stock reticle: HUD movie has no numeric reticle position");
            unbindable = true;
        } else {
            Logger::Instance().Info("Stock reticle bound: origin %.2f, %.2f", baseX, baseY);
        }
    }
    if (unbindable) return;

    double x = baseX, y = baseY;
    CameraFrame frame;
    const Mod& mod = Mod::Instance();
    const bool active = mod.GetConfig().showCrosshair && mod.IsEnabled()
                     && GameState::IsInGameplay() && GetCameraFrame(frame);
    if (active) {
        double offsetX = 0, offsetY = 0;
        if (!ReticleOffset(movie, root, frame, offsetX, offsetY)) {
            ReportTransientFailure("invalid HUD frame or parent scale");
            return;
        }
        x += offsetX;
        y += offsetY;
    }
    if (!SetNumber(root, kReticleX, x) || !SetNumber(root, kReticleY, y)) {
        ReportTransientFailure("could not set the HUD reticle position");
    }
}

void PositionHitMarker(uintptr_t menu) {
    uintptr_t movie = 0, root = 0;
    if (!cameraunlock::memory::SafeRead(menu + kMenuMovieOffset, movie) || !movie
        || !cameraunlock::memory::SafeRead(movie + kMovieRootOffset, root) || !root) return;

    constexpr char markerX[] = "root1.HitAndKillIndicator_mc.x";
    constexpr char markerY[] = "root1.HitAndKillIndicator_mc.y";
    static uintptr_t lastMovie = 0;
    static double baseX = 0, baseY = 0;
    if (movie != lastMovie) {
        if (!GetNumber(root, markerX, baseX) || !GetNumber(root, markerY, baseY)) {
            ReportTransientFailure("HUD movie has no numeric hit marker position");
            return;
        }
        lastMovie = movie;
        Logger::Instance().Info("Hit marker bound: origin %.2f, %.2f", baseX, baseY);
        const std::lock_guard<std::mutex> lock(g_impactMutex);
        g_hasImpact = false;
    }

    ImpactPoint impact;
    bool hasImpact;
    {
        const std::lock_guard<std::mutex> lock(g_impactMutex);
        impact = g_impact;
        hasImpact = g_hasImpact;
    }
    double x = baseX, y = baseY;
    CameraFrame frame;
    if (hasImpact && Mod::Instance().IsEnabled() && GameState::IsInGameplay() && GetCameraFrame(frame)) {
        using GetRect = MovieRect* (*)(uintptr_t, MovieRect*);
        const auto getRect = Virtual<GetRect>(movie, kSlotGetVisibleRect);
        MovieRect rect{};
        double scaleX = 0, scaleY = 0;
        if (!getRect || !GetNumber(root, "root1.scaleX", scaleX)
            || !GetNumber(root, "root1.scaleY", scaleY) || scaleX == 0 || scaleY == 0) {
            ReportTransientFailure("invalid hit marker parent scale");
            return;
        }
        getRect(movie, &rect);
        if (!(rect.right > rect.left) || !(rect.bottom > rect.top)) {
            ReportTransientFailure("invalid hit marker HUD frame");
            return;
        }
        float relative[3], ndcX = 0, ndcY = 0;
        g_relativeImpact(relative, &impact, frame.drawn.e);
        const bool projected = ProjectImpact(frame, relative, ndcX, ndcY);
        if (projected) {
            x += ndcX * (rect.right - rect.left) * 0.5 / scaleX;
            y -= ndcY * (rect.bottom - rect.top) * 0.5 / scaleY;
        } else {
            x += kOffscreenFrameWidths * (rect.right - rect.left) / scaleX;
        }
        static uint64_t lastLog = 0;
        const auto now = GetTickCount64();
        if (now - lastLog >= 1000) {
            lastLog = now;
            Logger::Instance().Info("hit marker: impact(%.3f,%.3f,%.3f) origin=%u projected=%d ndc(%+.4f,%+.4f) hud(%.2f,%.2f)",
                impact.position[0], impact.position[1], impact.position[2], impact.originIndex,
                projected, ndcX, ndcY, x, y);
        }
    }
    if (!SetNumber(root, markerX, x) || !SetNumber(root, markerY, y)) {
        ReportTransientFailure("could not set the HUD hit marker position");
    }
}

void UpdateHud(uintptr_t menu) {
    g_original(menu);
    __try {
        PositionReticle(menu);
        PositionHitMarker(menu);
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        ReportHudException(GetExceptionCode());
    }
}

HudUpdate g_shipOriginal = nullptr;
using SerializeHudFloat = void (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
SerializeHudFloat g_serializeFloatIndex = nullptr;
SerializeHudFloat g_serializeFloatName = nullptr;
uintptr_t g_stickDataVtable = 0;
constexpr uintptr_t kStickAimXOffset = 0x188;
constexpr uintptr_t kStickAimYOffset = 0x1a8;

void SerializeShipHudFloat(SerializeHudFloat original, uintptr_t object, uintptr_t context,
                           uintptr_t destination, uintptr_t key) {
    uintptr_t owner = 0, ownerVtable = 0;
    const bool readable = cameraunlock::memory::SafeRead(object + 8, owner);
    const bool aimX = readable && object - owner == kStickAimXOffset;
    const bool aimY = readable && object - owner == kStickAimYOffset;
    if ((aimX || aimY) && cameraunlock::memory::SafeRead(owner, ownerVtable)
        && ownerVtable == g_stickDataVtable) {
        const Mod& mod = Mod::Instance();
        CameraFrame frame{};
        float nx = 0, ny = 0;
        if (!mod.GetConfig().shipAimUIFollowsHead && mod.IsEnabled()
            && GameState::IsInGameplay() && GetCameraFrame(frame)
            && ProjectAimDirection(frame, nx, ny)) {
            // The target is already projected through the tracked camera. The
            // reticle container adds its offset after the HUD blends this point
            // with the ship heading, so remove that offset from this input only.
            alignas(8) unsigned char snapshot[0x20];
            if (!cameraunlock::memory::SafeRead(object, snapshot)) {
                Logger::Instance().Error("Ship lock UI: cannot read the outgoing float value");
                return;
            }
            float value = 0;
            memcpy(&value, snapshot + 0x18, sizeof(value));
            value += aimX ? -nx * 0.5f : ny * 0.5f;
            memcpy(snapshot + 0x18, &value, sizeof(value));
            original(reinterpret_cast<uintptr_t>(snapshot), context, destination, key);
            return;
        }
    }
    original(object, context, destination, key);
}

void SerializeShipHudFloatIndex(uintptr_t object, uintptr_t context, uintptr_t destination, uintptr_t key) {
    SerializeShipHudFloat(g_serializeFloatIndex, object, context, destination, key);
}

void SerializeShipHudFloatName(uintptr_t object, uintptr_t context, uintptr_t destination, uintptr_t key) {
    SerializeShipHudFloat(g_serializeFloatName, object, context, destination, key);
}

std::atomic<uintptr_t> g_shipMovie{0};
void UpdateShipHud(uintptr_t menu) {
    g_shipOriginal(menu);
    uintptr_t movie = 0;
    cameraunlock::memory::SafeRead(menu + kMenuMovieOffset, movie);
    g_shipMovie.store(movie, std::memory_order_relaxed);
}

using CaptureMovie = uint64_t (*)(uintptr_t, bool);
CaptureMovie g_captureOriginal = nullptr;
constexpr char kShipX[] = "root1.Menu_mc.Reticle_mc.ShipReticle_mc.x";
constexpr char kShipY[] = "root1.Menu_mc.Reticle_mc.ShipReticle_mc.y";

struct ShipReticlePosition {
    uintptr_t root = 0;
    double x = 0, y = 0;
    double offsetX = 0, offsetY = 0;
};

bool ReadShipReticlePosition(uintptr_t movie, const CameraFrame& frame, ShipReticlePosition& position) {
    uintptr_t root = 0;
    if (!cameraunlock::memory::SafeRead(movie + kMovieRootOffset, root) || !root) return false;
    position.root = root;
    if (!GetNumber(root, kShipX, position.x) || !GetNumber(root, kShipY, position.y)) return false;
    double sx = 1, sy = 1;
    for (const char* path : {"root1.scaleX", "root1.Menu_mc.scaleX", "root1.Menu_mc.Reticle_mc.scaleX"}) {
        double scale = 0;
        if (!GetNumber(root, path, scale) || scale == 0) return false;
        sx *= scale;
    }
    for (const char* path : {"root1.scaleY", "root1.Menu_mc.scaleY", "root1.Menu_mc.Reticle_mc.scaleY"}) {
        double scale = 0;
        if (!GetNumber(root, path, scale) || scale == 0) return false;
        sy *= scale;
    }
    using GetRect = MovieRect* (*)(uintptr_t, MovieRect*);
    const GetRect getRect = Virtual<GetRect>(movie, kSlotGetVisibleRect);
    if (!getRect) return false;
    MovieRect rect{};
    getRect(movie, &rect);
    if (!(rect.right > rect.left) || !(rect.bottom > rect.top)) return false;
    float nx = 0, ny = 0;
    if (ProjectAimDirection(frame, nx, ny)) {
        position.offsetX = nx * (rect.right - rect.left) * 0.5 / sx;
        position.offsetY = -ny * (rect.bottom - rect.top) * 0.5 / sy;
    } else {
        position.offsetX = kOffscreenFrameWidths * (rect.right - rect.left) / sx;
    }
    return std::isfinite(position.offsetX) && std::isfinite(position.offsetY);
}

bool PrepareShipReticle(uintptr_t movie, const CameraFrame& frame, ShipReticlePosition& position) {
    __try {
        return ReadShipReticlePosition(movie, frame, position);
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        ReportHudException(GetExceptionCode(), "reading the ship aim UI");
        return false;
    }
}

bool SetShipReticlePosition(const ShipReticlePosition& position, bool shifted) {
    __try {
        const bool xSet = SetNumber(position.root, kShipX, position.x + (shifted ? position.offsetX : 0));
        const bool ySet = SetNumber(position.root, kShipY, position.y + (shifted ? position.offsetY : 0));
        return xSet && ySet;
    } __except (cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        ReportHudException(GetExceptionCode(), "positioning the ship aim UI");
        return false;
    }
}

uint64_t CaptureShipMovie(uintptr_t movie, bool onlyChanges) {
    const Mod& mod = Mod::Instance();
    CameraFrame frame{};
    if (movie != g_shipMovie.load(std::memory_order_relaxed)
        || mod.GetConfig().shipAimUIFollowsHead || !mod.IsEnabled()
        || !GameState::IsInGameplay() || !GetCameraFrame(frame)) {
        return g_captureOriginal(movie, onlyChanges);
    }

    ShipReticlePosition position;
    if (!PrepareShipReticle(movie, frame, position)) {
        ReportTransientFailure("could not read the ship aim UI layout");
        return g_captureOriginal(movie, onlyChanges);
    }
    if (!SetShipReticlePosition(position, true)) {
        ReportTransientFailure("could not position the ship aim UI");
        if (!SetShipReticlePosition(position, false)) ReportTransientFailure("could not restore the ship aim UI");
        return g_captureOriginal(movie, onlyChanges);
    }

    // The small reticle uses globalToLocal during the movie update. Keep the
    // game's layout intact until it captures its render snapshot, and restore
    // it before the next update so animation and input never see our offset.
    const uint64_t result = g_captureOriginal(movie, onlyChanges);
    if (!SetShipReticlePosition(position, false)) ReportTransientFailure("could not restore the ship aim UI");
    return result;
}

}

bool InstallStockReticleHook() {
    const BuildProfile* profile = ResolveBuildProfile();
    if (!profile) return false;
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    uintptr_t base = 0;
    size_t moduleSize = 0;
    if (!gameModule) return false;
    if (!cameraunlock::memory::GetModuleRange(gameModule, base, moduleSize)) return false;
    // g_releaseValue is called as a function pointer, so an RVA that does not
    // land inside the running image is a call into whatever is mapped there.
    if (profile->hudUpdateRva >= moduleSize || profile->gfxReleaseValueRva >= moduleSize) {
        Logger::Instance().Error("Stock reticle: a build profile RVA is outside the module");
        return false;
    }
    g_releaseValue = reinterpret_cast<void (*)(GfxNumber*)>(base + profile->gfxReleaseValueRva);
    g_target = reinterpret_cast<void*>(base + profile->hudUpdateRva);
    const MH_STATUS status = MH_CreateHook(g_target, reinterpret_cast<void*>(&UpdateHud),
                                           reinterpret_cast<void**>(&g_original));
    if (status != MH_OK) {
        Logger::Instance().Error("Stock reticle: HUD hook failed: %s", MH_StatusToString(status));
        g_target = nullptr;
        return false;
    }
    Logger::Instance().Info("Stock reticle HUD hook installed");
    if (profile->worldOriginIndexRva + sizeof(uint32_t) > moduleSize
        || profile->relativeAimPointRva >= moduleSize
        || profile->playerHitEventRva >= moduleSize || profile->hudHitEventRva >= moduleSize) {
        Logger::Instance().Error("Hit marker: a build profile RVA is outside the module");
        return false;
    }
    g_worldOriginIndex = base + profile->worldOriginIndexRva;
    g_relativeImpact = reinterpret_cast<RelativeImpact>(base + profile->relativeAimPointRva);
    for (const auto& hook : {std::pair{profile->playerHitEventRva, &PlayerHitEvent},
                             std::pair{profile->hudHitEventRva, &HudHitEvent}}) {
        auto* original = hook.first == profile->playerHitEventRva ? &g_playerHitOriginal : &g_hudHitOriginal;
        const auto hitStatus = MH_CreateHook(reinterpret_cast<void*>(base + hook.first),
            reinterpret_cast<void*>(hook.second), reinterpret_cast<void**>(original));
        if (hitStatus != MH_OK) {
            Logger::Instance().Error("Hit marker hook: %s", MH_StatusToString(hitStatus));
            return false;
        }
    }
    Logger::Instance().Info("Hit marker impact hooks installed");
    return true;
}

bool InstallShipReticleHook() {
    const BuildProfile* profile = ResolveBuildProfile();
    if (!profile) return false;
    uintptr_t base = 0;
    size_t moduleSize = 0;
    if (!cameraunlock::memory::GetModuleRange(GetModuleHandleA(GAME_EXE), base, moduleSize)) return false;
    if (profile->gfxReleaseValueRva >= moduleSize) {
        Logger::Instance().Error("Ship aim UI: release-value RVA is outside the module");
        return false;
    }
    g_releaseValue = reinterpret_cast<void (*)(GfxNumber*)>(base + profile->gfxReleaseValueRva);
    const uintptr_t shipVtable = FindVtableByRTTI(base, moduleSize, ".?AVSpaceshipHudMenu@@");
    uintptr_t shipUpdate = 0;
    const uintptr_t movieVtable = FindVtableByRTTI(base, moduleSize, ".?AVMovieImpl@GFx@Scaleform@@");
    uintptr_t capture = 0;
    if (!shipVtable || !movieVtable
        || !cameraunlock::memory::SafeRead(shipVtable + 11 * sizeof(uintptr_t), shipUpdate)
        || !cameraunlock::memory::SafeRead(movieVtable + 25 * sizeof(uintptr_t), capture)
        || shipUpdate < base || shipUpdate - base >= moduleSize
        || capture < base || capture - base >= moduleSize) {
        Logger::Instance().Error("Ship aim UI: menu update or movie capture is unavailable");
        return false;
    }
    const MH_STATUS shipStatus = MH_CreateHook(reinterpret_cast<void*>(shipUpdate),
        reinterpret_cast<void*>(&UpdateShipHud), reinterpret_cast<void**>(&g_shipOriginal));
    if (shipStatus != MH_OK) {
        Logger::Instance().Error("Ship HUD hook: %s", MH_StatusToString(shipStatus));
        return false;
    }
    const MH_STATUS captureStatus = MH_CreateHook(reinterpret_cast<void*>(capture),
        reinterpret_cast<void*>(&CaptureShipMovie), reinterpret_cast<void**>(&g_captureOriginal));
    if (captureStatus != MH_OK) {
        Logger::Instance().Error("Ship capture hook: %s", MH_StatusToString(captureStatus));
        return false;
    }
    Logger::Instance().Info("Ship aim UI: %s", Mod::Instance().GetConfig().shipAimUIFollowsHead
        ? "head-fixed" : "anchored to the forward view");
    g_stickDataVtable = FindVtableByRTTI(base, moduleSize, ".?AV?$TUIDataToFlash@UStickData@@@@");
    const uintptr_t floatVtable = FindVtableByRTTI(base, moduleSize, ".?AV?$TUIValue@M@@");
    if (!g_stickDataVtable || !floatVtable) {
        Logger::Instance().Error("Ship lock UI: float serializer or stick data type is unavailable");
        return false;
    }
    auto slots = reinterpret_cast<SerializeHudFloat*>(floatVtable + sizeof(uintptr_t));
    g_serializeFloatIndex = slots[0];
    g_serializeFloatName = slots[1];
    // These serializers start with VEX instructions unsupported by MinHook.
    // Swap their virtual slots; each wrapper only reads the snapshot's float.
    DWORD oldProtection = 0;
    if (!VirtualProtect(slots, 2 * sizeof(*slots), PAGE_READWRITE, &oldProtection)) {
        Logger::Instance().Error("Ship lock UI: cannot protect serializer slots (%lu)", GetLastError());
        return false;
    }
    slots[0] = &SerializeShipHudFloatIndex;
    slots[1] = &SerializeShipHudFloatName;
    DWORD unusedProtection = 0;
    if (!VirtualProtect(slots, 2 * sizeof(*slots), oldProtection, &unusedProtection)) {
        Logger::Instance().Error("Ship lock UI: cannot restore serializer protection (%lu)", GetLastError());
        return false;
    }
    return true;
}

}
