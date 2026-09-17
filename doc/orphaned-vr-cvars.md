# PreyVR: 102 of 124 `vr_*` cvars have no reader, and two are declared twice

*Found while porting PreyVR v1.2.4 to PCVR. **None of this is caused by that
port** — every finding below was verified directly against
`Team-Beef-Studios/PreyVR`, and all 102 are equally unread there. Written up in
case it is useful.*

## The short version

PreyVR declares 124 `vr_*` cvars. **22 are read somewhere. 102 are not read
anywhere at all** — not by the engine, not by the game module, not by any `.gui`.

Most of the 102 ship in `preyconfig.cfg`, each with a description that reads like
a feature. Setting them does nothing.

Separately, and more concretely: **`vr_shakeAmplitude` is declared twice with
different defaults**, which the engine warns about on every map load.

## `vr_shakeAmplitude` / `vr_shakeamplitude`

```
neo/game/gamesys/SysCvar.cpp:413   idCVar vr_shakeamplitude(  "vr_shakeamplitude", "0.8", ... )
neo/game/Vr.cpp:98                 idCVar vr_shakeAmplitude(  "vr_shakeAmplitude", "1.0", ... )
```

Doom 3 cvar names are case-insensitive, so these are one cvar registered twice
with two different initial values. Every map load prints:

```
WARNING: CVar 'vr_shakeamplitude' declared multiple times with different initial value
```

Five sites read `vr_shakeAmplitude` — `PlayerView.cpp` twice,
`Prey/game_player.cpp`, `prey_baseweapons.cpp`, `prey_weaponfirecontroller.cpp` —
so screen shake works, but whether its default is 0.8 or 1.0 depends on
registration order rather than intent.

`vr_knockback` and `vr_knockBack` are the same pattern, though both are unread so
nothing observable follows from it.

## The one likely to bite a player

`vr_heightAdjust` is live. `vr_heightoffset` is dead.

Two plausible names for the same idea, both declared, both described, one wired
up. Anyone tuning their height by editing `preyconfig.cfg` has even odds of
picking the one that does nothing and concluding the feature is broken.

## Where the 102 went

They are implemented — in DrBeef's Doom3Quest, which PreyVR forked from at
`88c4f854`:

| cvar | Doom3Quest | PreyVR |
|---|---|---|
| `vr_flashlightMode` | 29 refs | 0 |
| `vr_crouchMode` | 7 | 0 |
| `vr_guiMode` | 6 | 0 |
| `vr_hudPosHorz` | 5 | 0 |
| `vr_dualWield` | 2 | 0 |
| `vr_headshotMultiplier` | 2 | 0 |

Doom3Quest implements these inside Doom 3's game module, `neo/game/*`. PreyVR
replaced that module wholesale with Prey's `neo/game/Prey/*`. The declarations
live in `neo/game/Vr.cpp` and came across intact; the code that read them did
not, and nothing replaced it.

This is not a regression — those options have presumably never worked in PreyVR.
It is only visible because the declarations and the shipped config still
advertise them.

## The 22 that do work

```
display    vr_msaa  vr_supersampling  vr_refreshrate  vr_ipd  vr_scale
           vr_mixedReality
movement   vr_turnangle  vr_turnmode  vr_walkdirection  vr_heightAdjust
           vr_trackingScale  vr_switchSticks
weapons    vr_weaponHand  vr_weaponSight  vr_weaponSightToSurface
           vr_weaponToggle  vr_weaponWheel  vr_weaponWheelCurrent
other      vr_haptics  vr_hudType  vr_shakeAmplitude  vr_invertVehicleY
```

Almost all of these are PreyVR's own Prey-specific work — the weapon wheel, the
weapon sight, the HUD type, the vehicle inversion — plus the core display
settings. The pattern is clean: **what was written for Prey works; what was
inherited from Doom3Quest and not ported does not.**

## The 102, grouped

**HUD** (11) — `vr_hudLowHealth` `vr_hudOcclusion` `vr_hudPosAngle`
`vr_hudPosDist` `vr_hudPosHorz` `vr_hudPosLock` `vr_hudPosVert`
`vr_hudRevealAngle` `vr_hudScale` `vr_hudTransparency` `vr_hudmode`

**Flashlight** (8) — `vr_flashlightBodyPosX/Y/Z` `vr_flashlightHelmetPosX/Y/Z`
`vr_flashlightMode` `vr_flashlightStrict`

**Comfort and movement** (10) — `vr_comfortRepeat` `vr_crouchHideBody`
`vr_crouchMode` `vr_deadzonePitch` `vr_deadzoneYaw` `vr_forward_keyhole`
`vr_heightoffset` `vr_instantAccel` `vr_jumpBounce` `vr_motionSickness`

**Interaction** (13) — `vr_3dgui` `vr_PDAfixLocation` `vr_contextSensitive`
`vr_dualWield` `vr_gripMode` `vr_guiMode` `vr_guiScale` `vr_guiSeparation`
`vr_pdaPitch` `vr_pdaPosX/Y/Z` `vr_throwables`

**Combat** (8) — `vr_headKick` `vr_headshotMultiplier` and its four difficulty
variants, `vr_knockBack` `vr_knockback`

**Controllers** (7) — `vr_autoSwitchControllers` `vr_controllerOffsetX/Y/Z`
`vr_handSwapsAnalogs` `vr_joystickMenuMapping` `vr_mountedWeaponController`

**Teleport locomotion** (9, inside "other" below) — `vr_teleport`
`vr_teleportButtonMode` `vr_teleportHint` `vr_teleportMaxTravel`
`vr_teleportMode` `vr_teleportShowAimAssist` `vr_teleportSkipHandrails`
`vr_teleportThroughDoors` and `vr_useFloorHeight`. The whole feature is declared
and absent.

**Everything else** (45) — `vr_cinematics` `vr_controlscheme` `vr_d3qversion`
`vr_debugHands` `vr_disableWeaponAnimation` `vr_frameCheck` `vr_headbbox`
`vr_motionFlashPitchAdj` `vr_motionWeaponPitchAdj` `vr_mountx/y/z`
`vr_moveClick` `vr_movePoint` `vr_moveThirdPerson` `vr_mustEmptyHands`
`vr_nodalX` `vr_nodalZ` `vr_normalViewHeight` `vr_playerBodyMode`
`vr_rumbleChainsaw` `vr_shakeamplitude` `vr_shotgunChoke` `vr_stepSmooth`
`vr_strobeTime` `vr_useHandPoses` `vr_vcx/y/z` `vr_walkSpeedAdjust`
`vr_weaponCycleMode` `vr_weaponPivotForearmLength`
`vr_weaponPivotOffsetForward/Horizontal/Vertical` `vr_wristStatMon`

## Reproducing it

`tools/headless/cvar_final.py` in the PCVR fork does the count, and
`--compare` runs it against a second tree to check whether anything is dead in
one and live in the other. No headset, no runtime, no game execution — source
analysis only.

**A warning about the method, because this audit was wrong twice before it was
right.**

1. The first version counted every occurrence of a name and treated two or fewer
   as dead. A declaration alone scores three — the identifier, the same text
   inside the quoted name, and the quoted name counted separately — so nothing
   could ever fall below the bar and it reported "none dead". A check that cannot
   fail.
2. The second used a shell `grep -E "idCVar[ \t]+name"` to exclude declarations.
   In POSIX ERE `[ \t]` is space, backslash, or the letter *t* — **not a tab**.
   Every cvar declared with a tab after `idCVar` had its own declaration counted
   as a use and came out live. That is where the earlier figure of 96 came from.

The count above excludes each declaration by line, in Python, where a tab is a
tab.
