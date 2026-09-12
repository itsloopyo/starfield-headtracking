#pragma once

#include <cstdint>

namespace StarfieldHT {

// Records which code touches a page of engine memory.
//
// Head tracking has to know the difference between the code that draws the
// frame and the code that decides where the player is aiming, because the two
// read the same camera and only one of them may see the head pose. Guessing
// which is which from the picture is not possible: a mod that rotates the
// camera for both looks right and shoots wrong.
//
// The page holding the camera is marked PAGE_GUARD, so the next access from any
// thread raises a guard-page exception naming both the instruction that made it
// and the address it touched. Only accesses that land inside [address, address +
// length) are recorded, which is what separates the camera's own basis from
// everything else sharing its page. The handler logs the instruction's offset in
// the game image, re-arms, and gives up after a fixed number of hits so the game
// is not left crawling.
//
// Developer aid. Nothing in a shipped build calls this.
bool ArmAccessProbe(uintptr_t address, size_t length, int maxHits);
void DisarmAccessProbe();

} // namespace StarfieldHT
