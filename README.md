# PreyVR — PCVR

A PC port of [lvonasek](https://github.com/lvonasek/PreyVR)'s **PreyVR**, the
Quest standalone VR build of Prey (2006), which itself is built on
[Team Beef](https://www.teambeefvr.com/)'s Doom3Quest and on
[emileb](https://github.com/emileb)'s multithreaded d3es, the engine that runs
Prey's game code on the GPL Doom 3 source. Runs on 64-bit Windows through
OpenXR against your headset's runtime.

The goal was reproduction, not reinterpretation: lvonasek designed and tuned
the VR experience, this moves it to PC hardware and changes as little else as
possible.

**Status: 1.0.x.** It plays end to end and has been installed from scratch on
two machines from a copy of Prey that was never on Steam. The weapon back
faces, the aim sight art and the weapon wheel art are this port's own rather
than PreyVR's - see Known issues for why and what that means to look at.

**This repository holds both targets.** The Windows port is added beside
lvonasek's Android code rather than replacing it - every change sits behind
`#if defined( _WIN32 )`, and `build.gradle`, `gradlew` and the Android manifest
are all still here and still build the Quest standalone version. Releases are
tagged by platform, so a `-pcvr` release is the PC build and a `-quest` release
would be the standalone one.

## Install

**You need your own copy of Prey (2006), patched to 1.4.** Seven files, and
where they come from - verified by reading the retail discs, not from memory:

| file | where it is |
|---|---|
| `pak000.pk4`, `pak001.pk4` | disc 1 |
| `pak002.pk4` | disc 2 |
| `pak003.pk4`, `pak004.pk4` | disc 3 |
| `pak005.pk4`, `pak006.pk4` | **the 1.4 patch** - on no disc |

A Steam or GOG copy already has all seven. A disc install has five of them and
needs the 1.4 patch applied, which is why an unpatched disc install will get
you most of the way and then stop: some of the files this port patches live
inside `pak005` and `pak006`.

Anything else in your `base` folder - `game00` to `game03`, or paks numbered
above 006 - is either a platform game binary this port does not use or a mod,
and Setup will not ask you for any of it.


A release is binaries, a launcher and the difference between Prey and PreyVR -
about 8 MB, with no game data in it at all.

1. Download the release zip and extract it anywhere.
2. Run **`Setup.bat`** once.
3. Start Virtual Desktop (or whichever OpenXR runtime you use) and put the
   headset on.
4. Run **`Play PreyVR.bat`**.

Setup finds Prey in your Steam libraries, or in your list of installed
programs, and copies the 13 `.pk4` files out of it - about 1.7 GB. If it
cannot find Prey it prints the list of files and you drag them, or the whole
`base` folder, onto the window. Nothing is downloaded and nothing is
installed; it runs on the PowerShell that ships with Windows and needs no
administrator. Running it again is harmless - files already in place are left
alone.

It then builds `vr_support.pk4`, the pack PreyVR adds, out of your own Prey
files. Most of what that pack contains is Prey's own - the menu, the HUD, the
weapon models - modified for VR, and those modifications are not the same thing
as the content they modify. So the release carries the modifications and your
install provides the content, which is why nothing here is anybody else's to
give away. If a file in your copy is not the version this was built against,
Setup says so and stops rather than building something subtly wrong; that would
mean a release or a language nobody has tried yet, and it is worth reporting.

If Prey is somewhere unusual, name it: `Setup.bat -PreyDir "D:\Games\Prey"`,
with no backslash on the end. To pick between two copies, `Setup.bat -Ask`.
Doing it by hand works only up to a point: the `.pk4` files go in
`preybase`, but `vr_support.pk4` has to be built, so run Setup at least
once even if you copy the rest yourself.

**You need** 64-bit Windows, your own copy of Prey (2006) patched to 1.4 -
the Steam version already is - and a PC VR headset with an OpenXR runtime.
Prey is delisted on Steam; a Steam key from a reseller still redeems and
installs. No game data is included here and none ever will be.

Config, saves and the log all live inside the folder (`saves\preybase`), so
backing up the folder backs up everything.

**Tested with** a Quest 3 over Virtual Desktop (the VDXR runtime) on an RTX
4070 Ti Super, at a locked 90 fps with every graphics setting at maximum.
SteamVR's OpenXR runtime starts the game and creates a session, but has not
been played through a headset; the Oculus runtime has not been tried.

### Settings

Everything ships at maximum. It is a 2006 game and a modern card should not
notice, but VR asks for roughly ten times the pixels a monitor does, so there
are two ways down.

**In game**, Options has Controls, Game, Video, VR and Cheats tabs. The VR tab
carries the settings this port adds: portal draw distance, whether the world is
locked to your frame rate, the desktop mirror, and smooth turn speed. Video
carries bloom strength and quality and texture filtering; Game carries master
volume.

**In a file**, `saves\preybase\autoexec.cfg` lists every option with a line
explaining it, all commented out. It is read after the shipped settings, so
anything you turn on there wins and nothing the game writes will overwrite your
choice. It opens with "if the frame rate is too low", in the order worth trying.

**The desktop mirror** (`pcvr_mirror`) puts one eye in a window of its own, for
streaming or recording, and follows whatever size you drag that window to. It is
off by default. It measured 17 microseconds a frame on the machine it was
written on, and the run report prints what it costs on yours.

### Flatscreen

`Play PreyVR flatscreen.bat` runs the game in a window with keyboard, mouse
and an XInput gamepad, touching no VR runtime at all. It is the same build
and the same VR-tuned game; it exists so the port can be looked at, and
played, without a headset. Edit the `-flatres` size in the bat to suit.

## Controls

Right-handed by default (`vr_weaponHand 0` swaps hands; `vr_switchSticks 1`
swaps the sticks). These are lvonasek's bindings, unchanged.

| | |
|---|---|
| Right trigger | fire |
| Right grip | alternate fire |
| Right stick left / right | turn - smooth by default at `pcvr_smoothTurnSpeed` degrees per second, `vr_turnmode 0` for snap at `vr_turnangle` degrees |
| Right stick up / down | weapon wheel (hold, pick, release); `vr_weaponToggle 0` makes it previous / next weapon instead. Also zooms the rifle |
| A | crouch |
| B | jump |
| Left stick | move, in the direction you look; `vr_walkdirection 1` follows the hand instead |
| Left stick click | spirit walk |
| Left trigger | run |
| Left grip | two-handed hold, which zooms the weapons that zoom |
| X | lighter |
| Y | throw grenade |
| Menu button (either hand) | pause menu |
| In menus | point and pull either trigger |

Height adjust, snap or smooth turn, weapon hand and the rest are on the
**Options → VR** page in the game's own menu. There is also a **Cheats** tab
on that page, exactly as on the Quest; it is lvonasek's and it is not hidden
here.

**Gamepad in flatscreen:** left stick moves, right stick looks, right
trigger fires, left trigger is alternate fire; every button lands on the
game's normal joystick binds (`pcvr_padDeadzone`, `pcvr_padLookSpeed`).

## Settings that differ from the Quest build

All of these are in the shipped config or are defaults, and every one of them
can be put back. They are the reason a first run looks the way it does.

| setting | Quest | here | why |
|---|---|---|---|
| `r_shadows` | 0 | **1** | shadows were off; a PC card does not need them off |
| `r_skipNewAmbient`, `r_usePhong` | off | **on** | the full lighting path |
| `image_anisotropy` | 0 | **16** | |
| `pcvr_bloomRange` | (3, fixed) | **3** | their radius, kept. The blur is 49 full-screen quads a frame and cost 8 fps at headset resolution when it was measured; `pcvr_bloomRange 1` is the fallback and now costs only blur quality |
| `pcvr_bloomScale` | (n/a) | **0.2** | new. The radius used to change the brightness - 49 quads at a fixed intensity each is 5.4x the light of 9 - so bloom got brighter as it got wider and highlights blew out. Brightness is normalised and separate now; 0.2 is the total light the 3x3 blur had, which is what the Quest build is played at. `1` is Prey's own look |
| `vr_msaa` | 4 | **0** | MSAA does not exist on this renderer's PC path; `vr_supersampling` is the lever instead. The VR menu still has an MSAA row, and on PC it is ignored outright: changing it bought nothing and forced a swapchain rebuild mid-session, which is the mechanism behind every freeze this port has had |
| `vr_refreshrate` | 72 | **90** | the runtime's own refresh rate wins on PC either way |
| `vr_turnmode` | 0, snap | **1, smooth** | `vr_turnangle` is still the snap angle at 45; the smooth rate is `pcvr_smoothTurnSpeed`, in degrees per second, because theirs was a per-frame step and so ran faster the higher the headset's refresh rate |

Everything else - weapon placement, the HUD, comfort settings, the 123 `vr_*`
cvars - is lvonasek's set with lvonasek's defaults. A separate note,
[`doc/orphaned-vr-cvars.md`](doc/orphaned-vr-cvars.md), lists the 102 of them
that are read by nothing on the Quest either.

One behavioural change from the Quest build, which is a fix rather than a
choice: three script-argument bugs from the 2006 game code (a `printf` format
handed to `sscanf`, so every float a map passed to a script arrived as zero)
are corrected. If a scripted set piece ever plays out differently from the
Quest, that is where to look first.

## Resolution and anti-aliasing

`vr_supersampling` is the only quality lever that matters here, and `1.0` -
the shipped value - already means "whatever your runtime asked for". On a Quest
3 over Virtual Desktop that is 3072x3264 **per eye**: about 20 megapixels a
frame, ninety times a second, where 4K is 8.3. It is not a floor to build on.

Raise it to `1.1` if you have headroom and want it sharper, then check
`PCVR late frames` in `saves/preybase/qconsole.log`. If that percentage climbs
much above a fraction of one percent, come back down. Remember your streaming
software's own resolution setting is already folded into that number, and the
picture is video-encoded on its way to the headset - past a point you are
rendering more pixels and pushing them through the same encoder.

If you cannot hold your headset's refresh rate, in this order, all measured:

1. `pcvr_bloomRange 1` - essentially free, only narrower blur
2. `r_shadows 0` - about 1 fps
3. `vr_supersampling 0.9`, then `0.8` - pixels scale quadratically, so this is
   the real lever on a weaker card

**Ignore `vr_msaa`.** It does nothing on PC; see the table above.

## Streaming and recording

`Options > VR > Desktop mirror` puts one eye in a borderless fullscreen window
on your desktop, for OBS. It costs about 25 microseconds a frame, so it will
not take frames off the headset.

The window follows your monitor's refresh rate by default. Be aware that a
90 Hz headset shown on a 60 Hz monitor cannot be smooth - 60 keeps two frames
out of every three, so motion advances 1-1-2, which is the same judder that
makes 24 fps film look wrong on a 60 Hz TV. Nothing in the port can remove
that; it is a display rate mismatch. A 120 Hz monitor makes it much less
visible, and `pcvr_mirrorHz 30` makes the cadence perfectly even at the cost of
running at 30. Everything is in `autoexec.cfg`.

## Where the weapons sit

Every weapon's position was tuned in a headset, and those numbers are one
person's arms and one person's grip. If a weapon sits wrong for you, the number
pad moves the one in your hands, an inch a tap:

| | | | |
|---|---|---|---|
| **KP 8** | forward | **KP 9** | up |
| **KP 2** | back | **KP 3** | down |
| **KP 4** | left | **KP 5** | print the current value |
| **KP 6** | right | **KP 0** | reset this weapon |

Each tap prints the running total at the top of the screen, so you can do it
without opening the console. Prey leaves the keypad unbound, so nothing else
is lost.

The values live in `pcvr_weaponNudge`, one `forward left up` triple per weapon,
and are saved with the rest of your settings.

## Known issues

- **The weapons are built from your own copy of Prey, not from PreyVR's
  models.** A flat shooter shows a viewmodel from one angle, so Human Head
  deleted the faces it could never reach and drew the player's hands and arms
  onto the weapon. In VR both decisions show. This port fixes them from the
  retail data itself:

  - the hands and forearms are a separate sub-mesh on four weapons, so they are
    pointed at a material that draws nothing. `vr_viewHands 1` puts them back.
  - the holes are closed with generated caps, measured by ray casting rather
    than by eye. Across the five affected weapons, the share of what you see
    that is the inside of the model falls from 25-35% to 3-4%.
  - each weapon's position was tuned in a headset. See **Where the weapons
    sit**, below, if yours do not suit you.

  What is left: the rifle's body mixes its outer shell with interior detail in
  one surface, so some of what you can see into is machinery you are meant to
  see. Barrels and the launcher's bore are open on purpose and are left that
  way.

- **The aim sight art is ours, not PreyVR's.** The feature is lvonasek's code
  and works the same way; the dot, ring, crosshair and beam are drawn
  procedurally by `tools/release/make_ui_art.py`, as is the weapon wheel. Every
  bitmap and model this release carries comes either from your own copy of Prey
  or from this port.


- **If frames drop, check the link before the game.** There was a fortnight of
  frame drops during testing that turned out to be the wireless network, not
  the port - and it affected other PCVR titles on the same machine, which is
  the tell. On a healthy link the game holds a median frame time of 11 ms at
  90 Hz, leaves 10 ms of the 11.1 ms period unused, and takes exactly one
  compositor slot per period on 97.4% of frames. If your numbers look nothing
  like that, the game is not the first place to look.

- **World GUIs are aimed with the head, not the hand.** The jukebox, wall
  consoles and keypads all trace from the eye along the view direction for 70
  units (`hhPlayer::UpdateFocus`, game_player.cpp). There is no hand-based gui
  aiming anywhere in the game code. Inherited from the Quest build; fixing it
  means pointing that ray with the controller, which changes every world gui in
  the game and is a feature rather than a port fix.

- **Portals go black beyond about fifteen feet.** Prey's portals show their far
  side only while the destination is already visible from where you stand.
  `info_portalflow`, the entity whose job is to open a PVS link between the two
  areas a portal joins, cannot spawn: its class sits inside `#if
  GAMEPORTAL_PVS`, and that switch cannot be turned on because
  `FindGamePortal`, `RegisterGamePortals` and `DrawGamePortals` are Human Head's
  additions to the Prey *renderer* - and Prey's engine source was never
  released, only its game code. They are absent from this tree, from emileb's
  d3es and from lvonasek's build. Every open-source Prey port has this.

  `pcvr_portalDistance` (default 0) lifts the two distance culls that sit above
  it, which took the threshold from about five feet to about fifteen. It cannot
  go further without implementing PVS-through-portals in the renderer.

- **Fourteen of Prey's own options are not in the menus, on purpose.** This
  engine declares them and reads them nowhere, so a control would move a number
  nothing looks at - which is worse than no control, because it looks like it
  works. They are `com_profanity`, `g_levelloadmusic`, `r_correctspecular`,
  `r_lowParticleDetail`, `r_normalizebumpmap`, `r_shaderlevel`,
  `r_skipGlowOverlay`, `r_skipNewAmbient`, `r_swapInterval`,
  `r_useFastSkinning`, `s_deviceName`, `s_musicvolume_dB`, `s_reverse` and
  `ui_showGun`. Three more are live but meaningless in a headset:
  `r_fullscreen`, `r_mode` and `r_multisamples` are window size and MSAA, and
  MSAA does not exist on this render path at all.

- **Mixed Reality does nothing here, and used to do something wrong.** The VR
  tab's Mixed Reality switch asks for Quest passthrough - `XR_FB_passthrough`,
  a Meta extension that does not exist on a PC runtime. Two of the three places
  it was read already checked for the extension; the third did not, and turning
  it on halved the field of view with no passthrough behind it. That one is
  guarded now, so the switch is simply inert on PC. Left in the menu rather
  than removed: it is lvonasek's row, and on the Quest build it works.

- **`vr_knockback` does nothing.** Declared twice and read nowhere. It sits in
  the config looking like a comfort option. Also inherited.

- Some warnings in `qconsole.log` are stock Prey and can be ignored: the
  missing `ui/assets/scrollbar_*` and `_menushot` images are Doom 3 leftovers
  that Prey never shipped, `guisounds_menuclickup` is declared by no sound
  shader in the retail data, `base_playerclip.lwo` genuinely has no UVs because
  it is collision geometry, and `SetClipModelAxis invalid newx` is Human Head's
  own handled edge case during a wall-walk transition.

- `xrRequestDisplayRefreshRateFB` fails on Virtual Desktop's runtime. Harmless -
  the game runs at whatever rate Virtual Desktop is set to.
- The Leech Gun doors in *Second Chances* can fail to open if the pickup's
  trigger is skipped. That is the 2006 game, not the port. If it happens:
  set `com_allowConsole 1`, open the console with the key under Escape, and
  enter `trigger turnondefensesrelay`.
- If the game crashes it writes `crash.txt` beside the executable with a
  module+offset stack. Send it with `saves\preybase\qconsole.log` when
  reporting.

## Building

Visual Studio 2019 Build Tools (MSVC 14.29) and CMake 3.20 are the tested
combination; the Android build in this tree is untouched and still builds
with gradle. Two prebuilt dependencies are needed and are not vendored:

- the `x86_64-w64-mingw32` directory of
  [dhewm3-libs](https://github.com/dhewm/dhewm3-libs) - SDL2, OpenAL Soft,
  ogg/vorbis, jpeg and zlib, the versions this engine expects;
- `openxr_loader.lib` built from
  [KhronosGroup/OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK).

```
set PREYVR_DEPS=C:\path\to\dhewm3-libs\x86_64-w64-mingw32
set PREYVR_OPENXR_LIB=C:\path\to\openxr_loader.lib
tools\build\configure.bat
tools\build\build.bat
```

Output is `build\Release\PreyVR.exe` and `gamex86_64.dll`. The runtime DLLs
from the two dependencies must sit beside the executable; `tools\release\package.py`
assembles a release folder and zip, builds `vr_support.pk4` from
`app/src/main/pk4`, applies the PC settings to lvonasek's config, and refuses
to package anything that looks like game data.

`PROGRESS.md` is the full record of the port, milestone by milestone, including
every wrong turn. `tools/headless` runs the game with no headset - SteamVR's
null driver for the engine, `-flat` for the picture - and is how 25 maps and 17
saves were swept before anyone put a headset on.

## Contributors to PreyVR

This port exists because of other people's work. lvonasek's own README credits
these contributors, and they are reproduced here because a port that drops the
credits of the thing it is built on has taken something without saying so:

| | |
|---|---|
| Programming | **Lubos** (lvonasek), **GLKarin** |
| Engine consulting | **DrBeef**, **Baggyg**, **Bummser**, **Stiefl525** |
| Weapons 3D modeling | **Lubos**, **LennyGuy20** |
| SideQuest listing | **Bummser** |

and, further up the chain, **Defunkt**, who made the laser pointer that
PreyVR's weapon sight came from by way of Team Beef's Doom3Quest. This port
draws its own sight art, but the feature is theirs.

and the early testers on the **Team Beef** Discord.

## Licence and credit

Everything built from this repository is **GPL v3** (`COPYING.txt`), the
licence of every layer beneath it: id Software's Doom 3 source, dhewm3,
gabrielcuvillier's d3wasm, glKarin's Android port of Prey's game code,
emileb's `d3es-multithread` - which is the engine this actually runs on, 245
of 270 untouched files matching it exactly - Team Beef's Doom3Quest and
lvonasek's PreyVR.

The runtime libraries have their own licences, collected with a manifest in
[`licenses/`](licenses/README.md). This software is based in part on the work
of the Independent JPEG Group.

PreyVR is lvonasek's work; its own history, contributors and feature list are
in [the upstream README](https://github.com/lvonasek/PreyVR#readme), and the
family tree of projects it is built from is `doc/family_tree.png`. The VR
design here - the weapon handling, the wheel, the comfort options, the
recreated set pieces - is theirs. This repository adds a Windows/OpenXR
platform layer, a flatscreen mode, an XInput gamepad, and the measurements
that chose the PC defaults.
