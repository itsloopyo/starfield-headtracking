# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Steam support. Version 1.16.244.0 from Steam now gets the same head tracking,
  aim separation and crosshair placement as the Xbox app / Game Pass release.
- The helmet light follows your head instead of your aim, turning 1.5 times as
  far as your head turns.

### Changed

- Head tracking pauses while the game's idle camera circles your character
  after you leave the controls alone.
- Conversations keep head tracking on.
- Fixed the view sometimes spinning and climbing away after loading a save.

## [0.0.0] - 2026-09-08

### Added

- First working build for the Xbox / Game Pass release of Starfield, package
  version 1.16.244.0. On any other build - the Steam release, or this one once
  it is patched - the mod stays dormant: it installs no hooks and modifies
  nothing, and says so in HeadTracking.log.
- 6DOF head tracking: yaw, pitch, roll and leaning, at 1:1 with the tracker.
- Horizon-locked or camera-local yaw, switchable with Page Down.
- Three-position tracking mode on Page Up: full, rotation only, position only.
- Tracking is suppressed outside gameplay. The gate reads the game's own camera
  state alongside the mouse capture, so a conversation, photo mode, the
  workshop's overhead view and the console's free camera all stop the head from
  moving the view.
- Look and aim are separated: the mouse or stick keeps the aim while the head
  moves the view. Point at something, turn your head away, and its interaction
  prompt stays where you are pointing.
- The crosshair follows the aim. The game's own crosshair is moved off centre
  onto what you are pointing at instead of staying in the middle of the screen,
  and it goes back to centre when tracking is switched off.
- Three ways to aim down the sights, cycled with Insert: stock sights with roll
  only, full tracking with the mod's mark, and full tracking and no extra mark.
  What a mode does to the head pose applies down the sights only, and hip fire is
  fully tracked in all three. The mod's own mark is drawn only while the sights
  are up, and only in the mode that has one.
