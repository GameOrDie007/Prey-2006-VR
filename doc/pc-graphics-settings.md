# PC graphics settings

PreyVR's shipped `preyconfig.cfg` is tuned for a Quest, and those values persist
into the PC build because they are `CVAR_ARCHIVE` — a config written on a
headset keeps its settings on a 4070 Ti Super. Several of them were costing real
image quality for no reason.

Every cvar below was checked to have readers in this tree before being touched.
That matters: `preyconfig.cfg` also ships names that are read by nothing, and
setting those achieves exactly nothing (see `doc/orphaned-vr-cvars.md`).

## What was changed, and from what

| cvar | was | now | why |
|---|---|---|---|
| `r_shadows` | 0 | **1** | shadows were **off** — the biggest single difference |
| `r_skipBloomFX` | 1 | **0** | bloom |
| `r_skipNewAmbient` | 1 | **0** | ambient pass |
| `r_usePhong` | 0 | **1** | phong shading |
| `image_anisotropy` | 1 | **16** | was effectively no anisotropic filtering |
| `com_videoRam` | 256 | **4096** | it was reporting 256 MB on a 16 GB card |
| `com_machineSpec` | 1 | **3** | ultra tier |

Already correct, left alone: `r_skipBump 0`, `r_skipSpecular 0`,
`image_downSize 0`, `image_downSizeBump 0`, `image_downSizeSpecular 0`,
`image_filter GL_LINEAR_MIPMAP_LINEAR`.

Verified after a run: the values survive being rewritten on quit, which is not
automatic — the engine rewrites the archived set on exit and drops anything it
does not recognise.

## Two things that are not available, despite looking like they are

**`image_useCompression`** is only ever *written*, by the `com_machineSpec`
presets in `Common.cpp`. It is never declared as an `idCVar` anywhere in this
tree, so setting it does nothing and the engine drops the line when it rewrites
the config. Uncompressed textures are not a switch this build has.

**`com_machineSpec` does not apply its preset on startup.** It only runs under
`if (sysDetect)`, which is a first launch with no config at all. It is set to 3
for consistency, but it is the individual cvars above doing the work — which is
also why setting it cannot stomp them.

Also worth knowing: the ultra preset would set `image_anisotropy` to 8. The 16
above is set directly and is higher than the preset would give.

## Performance

**Unmeasured, and not measurable without the headset.** Flat mode is pinned to a
60 Hz tick — `USERCMD_MSEC` is derived from `GetRefresh()`, which returns 60
there — so a flat run reports the tick, not the GPU. Rendering at 3993x4243
produced the same frame rate as 1280x720, which is the proof that the number
means nothing for this question.

`r_shadows` is the one to watch. Doom 3 uses stencil shadow volumes, which are
fill-rate bound, and fill rate is exactly what scales with 34 megapixels a
frame. Everything else on the list is almost certainly free.

It is also the easiest to undo: **Options → Shadows** in the in-game menu, no
config editing and no restart. `r_skipBloomFX`, `r_skipBump`, `r_skipSpecular`,
`r_usePhong` and `r_brightness` are all in that menu too.

## The shipped default

`app/src/main/assets/preyconfig.cfg` is still upstream's file, untouched, so
the Android build is unchanged. The PC release does not ship that file: the
release packager (`tools/release/package.py`) applies the table above to it
and writes the result as `saves/preybase/preyconfig.cfg` in the release - the
first-run config, seeded the same way the Quest launcher seeds its own. The
list of settings lives in the packager's `PC_SETTINGS`, in one place.

`pcvr_bloomRange` defaults to 1 in code as well, because a player who deletes
their config should not fall back to the 8 fps blur. See PROGRESS.md for the
measurement.
