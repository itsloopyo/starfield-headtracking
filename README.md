# Starfield Head Tracking

![Starfield running with this mod](https://raw.githubusercontent.com/itsloopyo/starfield-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Starfield that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - look around with your head while your mouse or controller controls aim. Ship lock-on acquisition follows your view.
- **6DOF positional tracking** - lean, peek and duck as well as turning your head
- **Helmet light follows your head** - the beam points where you look, not where you aim
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Starfield version 1.16.244.0, from Steam or from Xbox Game Pass. Both
  builds are supported. On any other version the mod stays inactive until it is
  updated.
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or a supported device, or a phone app that speaks the OpenTrack UDP protocol.
- Windows 10 or 11, 64-bit.

## Installation

### Standalone Installer

1. Download `StarfieldHeadTracking-v<version>-installer.zip` from the
   [Releases](https://github.com/itsloopyo/starfield-headtracking/releases)
   page.
2. Extract it anywhere.
3. Double-click `install.cmd`. The installer finds the game and puts Ultimate
   ASI Loader (as `winmm.dll`) and the mod (`StarfieldHeadTracking.asi`) next to
   `Starfield.exe`, along with `HeadTracking.ini`.
4. Configure your tracker to send UDP to `127.0.0.1` on port `4242`. See
   [Setting Up OpenTrack](#setting-up-opentrack) below.
5. Launch Starfield.

If the installer cannot find your game, point it at the install folder
yourself. Either pass the path as an argument:

```powershell
.\install.cmd "C:\XboxGames\Starfield\Content"
```

Or set the `STARFIELD_PATH` environment variable before running it:

```powershell
$env:STARFIELD_PATH = "C:\XboxGames\Starfield\Content"
.\install.cmd
```

### Manual Installation

Use these files from the installer ZIP:

1. Rename `vendor/ultimate-asi-loader/dinput8.dll` to `winmm.dll` and put it
   next to `Starfield.exe`. That is usually
   `C:\Program Files (x86)\Steam\steamapps\common\Starfield` for Steam and
   `C:\XboxGames\Starfield\Content` for the Xbox app.
2. Copy `plugins/StarfieldHeadTracking.asi` to the same folder. The mod writes its
   own `HeadTracking.ini` beside it on first launch.
3. Configure your tracker using the steps below, then launch Starfield.

## Setting Up OpenTrack

The mod accepts OpenTrack UDP tracking data on port `4242`.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### VR Headset

1. Connect the headset to the PC over Air Link, Virtual Desktop, or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on **UDP over network**, host `127.0.0.1`, port `4242`.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam, with no
markers and no IR hardware. Select it under **Input**, pick your camera in its
settings, and use the output settings above. How well it tracks depends on your
camera and your lighting, so try it before buying anything.

### Phone App

A phone app that sends OpenTrack UDP data can connect directly to the mod.
Point it at your PC's LAN IP address (run `ipconfig` to find it) on port `4242`.
Check your app for an OpenTrack output option. I made
[Headcam](https://headcam.app) so decent tracking was free for anybody with a
phone already in their pocket, and it sends this protocol.

Direct connection works best when the app filters its tracking data on the
phone. Headcam does this. Try direct connection first; if the view drifts or
shakes while you hold still, send the app's output to OpenTrack's **UDP over
network** input on another port, such as `5252`. Use OpenTrack's filters and
curves, then forward its output to `127.0.0.1:4242`.

Connections through `127.0.0.1` use `LocalSmoothing`. A phone on Wi-Fi uses
`RemoteSmoothing`, as does a tracker on this PC sending through its LAN address.

### Centering

Look straight ahead and use your tracker's center control.

## Controls

Two equivalent binding sets by default - use whichever your keyboard has. The
nav-cluster keys and the chords do exactly the same thing. Each is an entry in
the `[Hotkeys]` lists in `HeadTracking.ini`, so either can be rebound or
removed there.

| Action              | Nav-cluster | Chord          |
|---------------------|-------------|----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G` |
| Toggle horizon lock | `Page Down` | `Ctrl+Shift+H` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

The tracking mode and horizon lock are saved to `HeadTracking.ini` when you
change them, and the game starts in them next time. `End` changes the current
session only: head tracking starts on or off as `EnableOnStartup` says.

### Aiming down sights

Head tracking stays on while you aim. The weapon stays where your mouse or
controller points it, so with your head turned it sits off to one side with its
sights still lined up, and your rounds land where those sights point. Head
movement is scaled to the zoom, so a scope does not magnify it.

### Ship aim UI

The large aim circle stays anchored to the forward ship view as you turn your
head. Steering still moves the small reticle inside it. The power and hull
panels stay fixed on screen.

Ship lock-on acquisition follows where you look. The target marker stays
attached to the ship, but looking away can cause you to lose the lock.

### Helmet light

Your helmet light follows your head rather than your aim, and turns a little
further than the view does. When you turn your head your eyes end up past the
centre of the screen, so a beam matched to the view alone lands short of what
you are looking at. Leaning carries the light with your eye.

| Setting | Default | What it does |
| --- | --- | --- |
| `LightFollowsHead` | `true` | Point the light where you are looking. `false` leaves it on your aim |
| `LightMultiplier` | `1.5` | How far it turns relative to your head. `1.0` matches the view, `0` keeps it pointing along your aim |

Both are under `[Light]` in `HeadTracking.ini`.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `HeadTracking.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

Earlier versions of the mod used an older layout for this file. The first time this version starts, it converts the file once into the layout below and keeps the file as it was beside it as `HeadTracking.ini.pre-canonical`. `HeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `HeadTracking.ini.pre-canonical` back over `HeadTracking.ini`, which restores the old file.

With every setting at its default, the file reads:

```ini
; Starfield head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=4242

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=true
; true: yaw turns around the world's up axis and a lean moves along the ground.
; false: both follow the camera's own axes.
WorldSpaceYaw=true
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=true

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=0.0
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=0.15

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=true
; How far, in metres, leaning left or right can move the view.
PositionLimitX=0.3
; How far, in metres, raising your head can move the view.
PositionLimitY=0.2
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=0.2
; How far, in metres, leaning forward can move the view.
PositionLimitZ=0.4
; How far, in metres, leaning back can move the view.
PositionLimitZBack=0.1

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=End, Ctrl+Shift+Y
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=PageUp, Ctrl+Shift+G
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=PageDown, Ctrl+Shift+H

[Light]
; true: a light you carry points where you look instead of where you aim.
LightFollowsHead=true
; How far the light turns for each degree your head turns.
; 1 matches the view, 0 keeps the light on your aim.
LightMultiplier=1.5
```
<!-- /cameraunlock:config -->

Earlier versions also read these settings, which this version no longer
reads: `YawMultiplier`, `PitchMultiplier` and `RollMultiplier` under
`[Sensitivity]`, `SensitivityX`, `SensitivityY` and `SensitivityZ` under
`[Position]`, `[Crosshair] Show`, `[Ship] AimUIFollowsHead` and
`[Hotkeys] AdsModeKey`. The game's crosshair and the ship's aim circle always
follow your aim now, and head tracking stays on through the sights.

`WorldSpaceYaw=true` (default) keeps "up" locked to the world horizon. Yawing
while looking at the floor still pans left and right, and leaning still moves
your eye across the ground rather than into it. Set it to `false` to use the
camera's own axes for both, which is what you want if the camera is riding
something that banks. Toggle it live with `Page Down`.

The mod applies the head pose as your tracker sends it. Set sensitivity,
response curves and axis inversion in your tracker, so the same profile works
across games.

## Troubleshooting

- **Mod not loading.** Confirm `winmm.dll` and `StarfieldHeadTracking.asi` are
  next to `Starfield.exe`. The loader must be named `winmm.dll`. Check for
  `HeadTracking.log` in that folder after launching.
- **No tracking response.** Your tracker must be sending UDP to
  `127.0.0.1:4242`. `HeadTracking.log` records `First tracker sample received`
  the moment anything arrives, along with whether the connection was classed as
  local or remote; if that line is absent the tracker is not reaching the game,
  so confirm `UdpPort` matches and check Windows Firewall. For a phone app,
  use your PC's LAN address. If the log says the port could not be bound,
  close the other program using that port.
- **Jittery or unstable tracking.** Raise the smoothing value your tracker
  actually uses: `RemoteSmoothing` for a phone or another device on the network,
  `LocalSmoothing` for a tracker running on this PC. Each covers rotation and
  position together. Webcam tracking benefits most from brighter, more even
  lighting.
- **An axis runs the wrong way.** Adjust axis inversion in your tracker.
  In OpenTrack, look under Options. If movement feels wrong while looking
  steeply up or down, try toggling horizon lock with `Page Down`.
- **The weapon is off to one side when I aim down sights.** Your head is
  turned: the weapon stays on your aim and you are looking past it. Turn back to
  it, or move your aim to where you are looking.
- **Nothing happens in game, and the log says there is no build profile.**
  Your game build is unsupported. Check the
  [Releases page](https://github.com/itsloopyo/starfield-headtracking/releases)
  for an update. The mod remains inactive on an unrecognized build.
- **Reporting a crash.** `HeadTracking.log` sits next to `Starfield.exe` and is
  rewritten on every launch. The previous launch is kept as
  `HeadTracking.prev.log`. Attach both files to your report.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files and leaves `HeadTracking.ini`
in place, so your settings survive a reinstall. The ASI loader is only removed
if the installer put it there; if you already had your own, it is left alone.
Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires CMake 3.20 or newer, Visual Studio 2022 with the C++ workload, and
[pixi](https://pixi.sh). The mod targets MSVC x64 and needs no game installed.

```bash
git clone --recursive https://github.com/itsloopyo/starfield-headtracking
cd starfield-headtracking
pixi run build-release
pixi run test
pixi run package
```

`pixi run package` writes the installer ZIP to `release/`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. Bundled and linked third-party
components keep their own licenses; see
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Starfield (c) Bethesda Game Studios / Bethesda Softworks.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by
  ThirteenAG.
- [OpenTrack](https://github.com/opentrack/opentrack).
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, and the
  Hacker Disassembler Engine by Vyacheslav Patkov that it builds on.
- [inih](https://github.com/benhoyt/inih) by Ben Hoyt.
- [CommonLibSF](https://github.com/Starfield-Reverse-Engineering/CommonLibSF).
- [CameraUnlock Core](https://github.com/itsloopyo/cameraunlock-core), the shared
  head-tracking library behind this mod.

**Special thanks to Zarlorne from Nexus Mods for funding the Steam port**

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Bethesda Game
Studios or Bethesda Softworks. It requires a legitimately purchased copy of the
game. Use at your own risk.
