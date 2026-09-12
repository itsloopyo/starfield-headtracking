#pragma once

namespace StarfieldHT {

// What the head is allowed to do to the picture while the sights are up, and
// whether the mod draws the mark. It is a cycle rather than a setting because
// the three only tell apart by feel, down the sights, with a gun in hand.
enum class AdsMode {
    // Roll alone. Roll turns the picture about the view axis and leaves the aim
    // on the centre of it, so the game's own sights and crosshair stay true and
    // the sight picture is the stock one.
    StockRollOnly,
    // Full tracking with a custom marker during ADS.
    TrackedWithMark,
    // Full tracking with the stock reticle only.
    TrackedNoMark,
};

namespace AdsState {

// Re-reads the player's iron-sights flag. Called once per camera update.
void Update();
bool IsAiming();

} // namespace AdsState

} // namespace StarfieldHT
