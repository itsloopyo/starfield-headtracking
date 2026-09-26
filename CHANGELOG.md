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
  `CameraUnlock.ini`. `LightFollowsHead=false` leaves the helmet light on your
  aim, and `LightMultiplier` sets how far it turns relative to your head: `1.0`
  matches the view, `0` keeps it pointing along your aim.
- A setting set to `default` in `CameraUnlock.ini` takes its value from
  `Defaults.ini`, which every head tracking mod that keeps its settings in
  `CameraUnlock.ini` reads. Head tracking mods that keep their settings in
  another file do not read it, and neither do earlier versions of this mod.
  Writing a value in place of `default` changes that setting for this game
  only. When the mod saves a setting that a hotkey changed in game, it writes
  the new value in place of `default`, so that setting no longer follows
  `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows;
  `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or
  `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set,
  under Wine and Proton too; and
  `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's
  log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the
  built-in values, unless Windows runs the game as a packaged app. The mod never
  changes `Defaults.ini` after that.

### Changed

- Head tracking pauses while the game's idle camera circles your character
  after you leave the controls alone.
- Conversations keep head tracking on.
- Fixed the view sometimes spinning and climbing away after loading a save.
- Settings move to `CameraUnlock.ini` in the game folder, next to
  `Starfield.exe`. Earlier versions of the mod kept these settings in
  `HeadTracking.ini`, in the same folder. The first time this version starts
  and finds no `CameraUnlock.ini`, it reads your settings from
  `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes
  `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini`
  exists.
- A setting that the defaults the README shows set to `default` is written as
  `default` when the value imported for it equals its default at that start,
  which is the value `Defaults.ini` gives it, or the built-in value where
  `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other
  setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking
  mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these,
  where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you
    changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
- An older version of the mod reads `HeadTracking.ini` and never reads
  `CameraUnlock.ini`, so a setting you change after updating is not in
  `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read
  `HeadTracking.ini` again. To go back to the defaults, replace everything in
  `CameraUnlock.ini` with the defaults the README shows. Every setting they set
  to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that
  triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
  The chords are ordinary entries now, so they can be rebound or removed.
- The tracking mode (Page Up) and horizon lock (Page Down) are saved to
  `CameraUnlock.ini` when you change them, and the game starts in them next
  time. End still changes the current session only: head tracking starts on or
  off as `EnableOnStartup` says.
- `[Position] LimitY` bounded raising and lowering your head alike. It is
  imported into both `PositionLimitY` and `PositionLimitYDown`, which can now
  be set apart.
- Uninstalling leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so
  your settings survive a reinstall. Installing, by `install.cmd` or through
  the launcher, no longer writes a `HeadTracking.ini` into the game folder
  where there is none: the mod creates `CameraUnlock.ini` at its first start.

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
