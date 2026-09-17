# Licences

## The game engine and this port

Everything built from this repository - `PreyVR.exe` and `gamex86_64.dll` - is
**GNU General Public License v3**, the text of which is `COPYING.txt` at the
root. That is the licence of every layer underneath:

| layer | author | licence |
|---|---|---|
| Doom 3 GPL source | id Software | GPL v3 |
| dhewm3 | dhewm3 contributors | GPL v3 |
| d3es (Android engine port) | glKarin | GPL v3 |
| Doom3Quest (VR renderer and integration) | DrBeef, Team Beef | GPL v3 |
| PreyVR (the Prey game code and the Quest port) | Luboš Vonásek (lvonasek) | GPL v3 |
| this PCVR port | Ryan Moore | GPL v3 |

The source for the binaries is this repository, at the commit the release was
tagged from.

`preybase/vr_support.pk4` is built from `app/src/main/pk4/` - the weapon
models, weapon wheel, laser sight and menu fragments that PreyVR adds on top of
the retail data. They are lvonasek's and ship under the same licence as the
repository they came from.

**No Prey game data is included.** The retail `.pk4` files belong to their
publisher and the player supplies their own copy.

## The runtime libraries beside the executable

Prebuilt, unmodified, from the dhewm3-libs bundle and the Khronos OpenXR SDK.
Each one's licence text is in this folder.

| file | project | licence | text |
|---|---|---|---|
| `SDL2.dll` | Simple DirectMedia Layer 2 | zlib | `SDL2.txt` |
| `OpenAL32.dll` | OpenAL Soft | LGPL v2 | `OpenAL-Soft-LGPL-2.0.txt` |
| `libjpeg-8.dll` | Independent JPEG Group libjpeg | IJG | `libjpeg-IJG-README.txt` |
| `libogg-0.dll`, `libvorbis-0.dll`, `libvorbisfile-3.dll` | Xiph.org libogg / libvorbis | BSD 3-clause | `libogg-libvorbis-BSD.txt` |
| `zlib1.dll` | zlib | zlib | `zlib.txt` |
| `openxr_loader.dll` | Khronos OpenXR-SDK loader | Apache 2.0 | `OpenXR-Loader-Apache-2.0.txt` |

This software is based in part on the work of the Independent JPEG Group.

OpenAL Soft is linked dynamically, as its LGPL requires: replace `OpenAL32.dll`
with any compatible build and the game will use it.
