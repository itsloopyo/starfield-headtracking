# Starfield Head Tracking

![Starfield running with this mod](https://raw.githubusercontent.com/itsloopyo/starfield-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Starfield that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - look around with your head while your mouse or controller controls aim. Ship lock-on acquisition follows your view.
- **6DOF positional tracking** - lean, peek and duck as well as turning your head
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- Starfield version 1.16.244.0, from Steam or from the Xbox app / PC Game Pass. Both
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

Two equivalent binding sets - use whichever your keyboard has. The nav-cluster
keys and the chords do exactly the same thing.

| Action                   | Nav-cluster | Chord          |
|--------------------------|-------------|----------------|
| Toggle tracking          | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode      | `Page Up`   | `Ctrl+Shift+G` |
| Toggle horizon lock      | `Page Down` | `Ctrl+Shift+H` |
| Cycle what the sights do | `Insert`    | `Ctrl+Shift+U` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

### What the sights do

`Insert` / `Ctrl+Shift+U` cycles these modes while aiming down the sights.
The default is **Tracked, mod reticle**. Hip fire keeps full head tracking.

1. **Stock, roll only.** Head roll tilts the view. Turning and leaning are
   paused until you lower the weapon.
2. **Tracked, mod reticle.** Full head tracking down the sights, with the mod's
   own mark drawn along the direction you are aiming.
3. **Tracked, game reticle.** Full head tracking down the sights, with no extra
   mark drawn. The game's own crosshair still follows your aim.

### Ship aim UI

The large aim circle stays anchored to the forward ship view as you turn your
head. Steering still moves the small reticle inside it. The power and hull
panels stay fixed on screen.

Ship lock-on acquisition follows where you look. The target marker stays
attached to the ship, but looking away can cause you to lose the lock.

`AimUIFollowsHead` under `[Ship]` in `HeadTracking.ini` controls this behavior:

- `false` (default): the circle stays aligned with the ship and moves across
  the screen as you turn your head.
- `true`: the circle stays in the same place on your screen as you turn your
  head, following your view instead of staying aligned with the ship.

Restart the game after changing this setting.

## Configuration

`HeadTracking.ini` sits next to `Starfield.exe`. It is written with defaults on
first launch; delete it to reset. Updating the mod never overwrites it.

```ini
[Network]
; UDP port for OpenTrack data (default: 4242)
UDPPort=4242

[Sensitivity]
; Rotation sensitivity multipliers (1.0 = 1:1). Leave them at 1.0 and shape the
; pose in your tracker instead, so one profile behaves the same in every game.
YawMultiplier=1.0
PitchMultiplier=1.0
RollMultiplier=1.0
; Smoothing, applied to both rotation and position. The value is picked
; per connection from the packet source address.
; LocalSmoothing: tracker running on this machine (loopback).
; RemoteSmoothing: tracker on a remote network device (phone on WiFi).
; 0.0 = no smoothing, 1.0 = heavy. Raise for a noisier tracker - it
; costs perceived latency.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
; Position tracking sensitivity (0.0-5.0). Leave at 1.0 and shape the pose in
; your tracker instead, so one profile behaves the same in every game.
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; Position limits in meters (how far the camera can move). Nothing yet stops a
; lean at a wall, so these stay small enough to keep the view inside the room.
LimitX=0.30
LimitY=0.20
LimitZ=0.40
; Backward lean limit (prevents camera clipping through player model)
LimitZBack=0.10
; Enable/disable position tracking (6DOF)
Enabled=true

[Hotkeys]
; Virtual key codes (hex)
ToggleKey=0x23         ; End - Enable/disable head tracking
PositionToggleKey=0x21 ; Page Up - Cycle tracking mode
YawModeKey=0x22        ; Page Down - Toggle world/local yaw
AdsModeKey=0x2D        ; Insert - Cycle what the sights do

[General]
; Auto-enable tracking on game start
AutoEnable=true
; Horizon lock: true (default) turns head yaw about the world's up axis and
; moves a lean along the ground, whatever the camera is pitched or rolled to.
; false uses the camera's own axes for both.
WorldSpaceYaw=true

[Crosshair]
; Reposition the game's native crosshair to follow your aim once
; head tracking moves the view. Set false to leave it at centre.
Show=true

[Ship]
; false anchors the aim circle to the forward view. true keeps it head-fixed.
; Restart the game after changing this setting.
AimUIFollowsHead=false
```

`WorldSpaceYaw=true` (default) keeps "up" locked to the world horizon. Yawing
while looking at the floor still pans left and right, and leaning still moves
your eye across the ground rather than into it. Set it to `false` to use the
camera's own axes for both, which is what you want if the camera is riding
something that banks. Toggle it live with `Page Down`.

Leave the sensitivity multipliers at `1.0` for 1:1 tracking. Configure response
curves and axis inversion in your tracker so the same profile works across games.

## Troubleshooting

- **Mod not loading.** Confirm `winmm.dll` and `StarfieldHeadTracking.asi` are
  next to `Starfield.exe`. The loader must be named `winmm.dll`. Check for
  `HeadTracking.log` in that folder after launching.
- **No tracking response.** Your tracker must be sending UDP to
  `127.0.0.1:4242`. `HeadTracking.log` records `First tracker sample received`
  the moment anything arrives, along with whether the connection was classed as
  local or remote; if that line is absent the tracker is not reaching the game,
  so confirm `UDPPort` matches and check Windows Firewall. For a phone app,
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

Run `uninstall.cmd`. This removes the mod files. The ASI loader is only removed
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

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

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

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Bethesda Game
Studios or Bethesda Softworks. It requires a legitimately purchased copy of the
game. Use at your own risk.
