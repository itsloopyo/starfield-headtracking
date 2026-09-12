#pragma once

namespace StarfieldHT {

// One-shot log of what the mod can reach of the HUD: the UI singleton, the HUD
// menu and the Scaleform movie behind it. Dev builds only; nothing in a shipped
// build calls it.
void ProbeHud();

} // namespace StarfieldHT
