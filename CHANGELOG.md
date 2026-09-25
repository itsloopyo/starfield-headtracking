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
- `LightFollowsHead` and `LightMultiplier` under `[Light]` in
  `HeadTracking.ini`. `LightFollowsHead=false` leaves the helmet light on your
  aim, and `LightMultiplier` sets how far it turns relative to your head: `1.0`
  matches the view, `0` keeps it pointing along your aim.

### Changed

- Head tracking pauses while the game's idle camera circles your character
  after you leave the controls alone.
- Conversations keep head tracking on.
- Fixed the view sometimes spinning and climbing away after loading a save.
- `HeadTracking.ini` has a new layout. The first time this version starts, it
  converts the file once into the new layout and keeps the file as it was
  beside it as `HeadTracking.ini.pre-canonical`.
  `HeadTracking.ini.pre-canonical.last`, when present, is the file as it was
  before the most recent conversion: the mod converts the file again when it
  finds the older layout later, for example after an older version of the mod
  rewrote it.
- Comments, and keys the mod never read, are not carried over. Nor are these,
  where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you
    changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
- Hotkeys are written as key names, and each hotkey lists every key that
  triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
  The chords are ordinary entries now, so they can be rebound or removed.
- An older version of the mod may not read the new layout correctly. It reads a
  key that moved as its own default, and it can misread a hotkey or another
  value that is now written as a name. To go back to an older version, first
  copy `HeadTracking.ini.pre-canonical` back over `HeadTracking.ini`, which
  restores the old file.
- The tracking mode (Page Up) and horizon lock (Page Down) are saved to
  `HeadTracking.ini` when you change them, and the game starts in them next
  time. End still changes the current session only: head tracking starts on or
  off as `EnableOnStartup` says.
- `[Position] LimitY` bounded raising and lowering your head alike. It converts
  into both `PositionLimitY` and `PositionLimitYDown`, which can now be set
  apart.
- Uninstalling leaves `HeadTracking.ini` in place, so your settings survive a
  reinstall.

### Removed

- The sensitivity settings: `YawMultiplier`, `PitchMultiplier` and
  `RollMultiplier` under `[Sensitivity]`, and `SensitivityX`, `SensitivityY`
  and `SensitivityZ` under `[Position]`. Set these in your tracker app instead.
  With these settings at their shipped defaults the camera moves as it did
  before.
- `[Crosshair] Show` and `[Ship] AimUIFollowsHead`. The game's crosshair and the
  ship's aim circle always follow your aim.
- `[Hotkeys] AdsModeKey`. Neither its key (Insert unless you changed it) nor
  Ctrl+Shift+U cycles what the sights do any more: head tracking carries on
  through the sights (be563fd).

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
