# GLES headers (PCVR)

Khronos OpenGL ES headers, vendored for the Windows build only.

On Android these come from the NDK, so their tree has never needed them. Windows
has no GLES header set at all, and this engine is written against GLES 2/3
throughout — `renderer/qgl.h` includes `<GLES2/gl2.h>`, and
`renderer/draw_gles3_multiview.cpp` includes `<GLES3/gl3.h>`.

Using the real ES headers rather than desktop GL ones is the 1:1 choice, not a
convenience. Their renderer is GLES3 with `GL_OVR_multiview2`, and all 17 of
their shaders are `#version 300 es`. The Milestone 1 probe measured that this
driver compiles those shaders unmodified and grants a two-layer array swapchain,
so there is nothing to gain by translating any of it to a desktop dialect —
only fidelity to lose. Enum values are identical between GL and GLES for
everything this engine touches.

These headers declare prototypes, but on Windows `opengl32.lib` exports only
GL 1.1. Everything past that is resolved at runtime; see the loader that
accompanies this directory.

Vendored the same way `../OpenXR/` is, and for the same reason.

| file | source |
|---|---|
| `GLES2/gl2.h`, `gl2ext.h`, `gl2platform.h` | https://registry.khronos.org/OpenGL/api/GLES2/ |
| `GLES3/gl3.h`, `gl32.h`, `gl3platform.h` | https://registry.khronos.org/OpenGL/api/GLES3/ |
| `KHR/khrplatform.h` | https://registry.khronos.org/EGL/api/KHR/ |

Fetched 2026-08-26. Unmodified.
