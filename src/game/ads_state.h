#pragma once

namespace StarfieldHT::AdsState {

// Re-reads the player's iron-sights flag. Called once per camera update, so the
// state is polled and never latched: a missed edge cannot leave the sights
// reading up through hip fire.
void Update();

// False whenever the flag could not be read.
bool IsAiming();

} // namespace StarfieldHT::AdsState
