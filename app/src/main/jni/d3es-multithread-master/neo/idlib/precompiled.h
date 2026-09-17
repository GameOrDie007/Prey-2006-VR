/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __PRECOMPILED_H__
#define __PRECOMPILED_H__

#ifdef __cplusplus

//-----------------------------------------------------

#if defined(_K_DEV)
#define LOGI(fmt, args...) {common->Printf(fmt, ##args); common->Printf("\n");}
#define LOGW(fmt, args...) common->Warning(fmt, ##args);
#define LOGE(fmt, args...) common->Error(fmt, ##args);
#endif

#define ID_TIME_T time_t
#ifdef _RAVEN
typedef unsigned char		byte;		// 8 bits

#ifndef BIT
#define BIT( num )				BITT< num >::VALUE
#endif

#ifndef Q4BIT
#define Q4BIT( num )				( 1 << ( num ) )
#endif

template< unsigned int B >
class BITT {
public:
	typedef enum bitValue_e {
		VALUE = 1 << B,
	} bitValue_t;
};
#endif

#ifdef _WIN32

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// prevent auto literal to string conversion

#ifndef GAME_DLL

// PCVR: what follows here originally was the 2004 Windows client build - MFC
// through tools/comafx/StdAfx.h, plus DirectInput and DirectSound. None of it
// applies to this fork:
//
//   * the MFC tool editors are gone (tools/radiant was deleted, and Android.mk
//     references src_tools without ever assigning it), and ID_ALLOW_TOOLS is
//     now off, so nothing references them;
//   * input and sound went to SDL and OpenAL long ago - dsound.h and dinput.h
//     back backends this tree no longer contains;
//   * `#define WINVER 0x501` pins the SDK to Windows XP, which makes a modern
//     Windows Kit fail on its own headers (LCMapStringEx unresolved, the
//     GC_/GID_ gesture constants undeclared) - 111 of the 229 errors in the
//     first engine sweep came from this one line and the MFC include below it.
//
// The Android build reaches none of this, so removing it is what makes the
// Windows build match lvonasek's rather than diverge from it. winsock2.h stays, and
// stays *before* the <windows.h> below, because that ordering is required.
#include <winsock2.h>
#include <mmsystem.h>
#include <mmreg.h>

#endif /* !GAME_DLL */

#pragma warning(disable : 4100)				// unreferenced formal parameter
#pragma warning(disable : 4244)				// conversion to smaller type, possible loss of data
#pragma warning(disable : 4714)				// function marked as __forceinline not inlined
#pragma warning(disable : 4996)				// unsafe string operations

#include <malloc.h>							// no malloc.h on mac or unix
#include <windows.h>						// for qgl.h
#undef FindText								// stupid namespace poluting Microsoft monkeys

#endif /* _WIN32 */

//-----------------------------------------------------

#if !defined( _DEBUG ) && !defined( NDEBUG )
// don't generate asserts
#define NDEBUG
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <ctype.h>
#include <typeinfo>
#include <errno.h>
#include <math.h>

//k 64
#include <inttypes.h>

#ifdef _HUMANHEAD
#ifndef TRUE
#define TRUE true
#endif
#ifndef FALSE
#define FALSE false
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

// PCVR: Windows declares all three of these itself, with different underlying
// types (DWORD is unsigned long, BOOL is int, INT_PTR is in basetsd.h), so
// redeclaring them here is a hard C2371 the moment windows.h is in scope.
// Android has no windows.h, which is why the Prey graft needed them.
//
// Measured before changing: BOOL has 2 real uses engine-wide, both
// `if((BOOL)boneDir)` where boneDir is an int - identical under bool and int.
// DWORD has 1 real use, `logitechLCDButtonsLast`, on a code path jmarshall
// already disabled. INT_PTR has 21 uses and Windows' definition is the more
// correct one. So this is a platform seam, not a behaviour change.
//
// Note the max() macro above is deliberately left alone: 7 call sites in
// game/Prey rely on it, nothing here includes <algorithm>, and windows.h
// defines a compatible one. Do not add NOMINMAX.
#if !defined( _WIN32 )
typedef intptr_t INT_PTR;
typedef unsigned int DWORD;
typedef bool BOOL;
#endif
#endif

#define round_up(x, y)	(((x) + ((y)-1)) & ~((y)-1))

//-----------------------------------------------------

// PCVR: sys_public.h carries ID_INLINE, ID_STATIC_TEMPLATE, ALIGN16, PACKED,
// _alloca16 and PATHSEPERATOR_* inside a single `#ifdef __linux__` block, so on
// Windows none of them exist and idlib/Lib.h fails at `template<class T>
// ID_INLINE T Max(...)`. Both that block and this precompiled.h are additions
// of the Prey graft - stock d3es has no precompiled.h at all and puts the
// platform macros in sys/platform.h, which is still present here, still has a
// correct `#ifdef _MSC_VER` branch, and is simply never reached because
// nothing on the main include path includes it.
//
// So use their file rather than inventing defines. Guarded to _WIN32 so the
// Android build is bit-for-bit unaffected: on Linux this is inert and
// sys_public.h keeps supplying the macros exactly as before.
#if defined( _WIN32 )
#include "../sys/platform.h"
#endif

// non-portable system services
#include "../sys/sys_public.h"

// id lib
#include "../idlib/Lib.h"
#ifdef _RAVEN // raven.h
#include "../raven/idlib/containers/Pair.h"
#include "../raven/idlib/math/Interpolate.h"
#include "../raven/idlib/rvMemSys.h"
// RAVEN BEGIN
// jsinger: add AutoPtr and text-to-binary compiler support
#include "../raven/idlib/AutoPtr.h"
#include "../raven/idlib/LexerFactory.h"
// jsinger: AutoCrit.h contains classes which aid in code synchronization
//          AutoAcquire.h contains a class that aids in thread acquisition of the direct3D device for xenon
//          Both compile out completely if the #define's above are not present
#include "../raven/idlib/threads/AutoCrit.h"
// RAVEN END

class ThreadedAlloc;		// class that is only used to expand the AutoCrit template to tag allocs/frees called from inside the R_AddModelSurfaces call graph
#endif

// framework
#include "../framework/BuildVersion.h"
#include "../framework/BuildDefines.h"
#include "../framework/Licensee.h"
#include "../framework/CmdSystem.h"
#include "../framework/CVarSystem.h"
#include "../framework/Common.h"
#include "../framework/File.h"
#include "../framework/FileSystem.h"
#include "../framework/UsercmdGen.h"

// decls
#include "../framework/DeclManager.h"
#include "../framework/DeclTable.h"
#include "../framework/DeclSkin.h"
#include "../framework/DeclEntityDef.h"
#include "../framework/DeclFX.h"
#include "../framework/DeclParticle.h"
#include "../framework/DeclAF.h"
#include "../framework/DeclPDA.h"
#ifdef _HUMANHEAD
#include "../framework/declPreyBeam.h" // HUMANHEAD CJR
#endif

// We have expression parsing and evaluation code in multiple places:
// materials, sound shaders, and guis. We should unify them.
const int MAX_EXPRESSION_OPS = 4096;
const int MAX_EXPRESSION_REGISTERS = 4096;

// renderer
#include "../renderer/qgl.h"
#include "../renderer/Cinematic.h"
#include "../renderer/Material.h"
#include "../renderer/Model.h"
#include "../renderer/ModelManager.h"
#include "../renderer/RenderSystem.h"
#include "../renderer/RenderWorld.h"

// sound engine
#include "../sound/sound.h"

// asynchronous networking
#include "../framework/async/NetworkSystem.h"

// user interfaces
#include "../ui/ListGUI.h"
#include "../ui/UserInterface.h"

// collision detection system
#include "../cm/CollisionModel.h"

// AAS files and manager
#include "../tools/compilers/aas/AASFile.h"
#include "../tools/compilers/aas/AASFileManager.h"

#ifdef _RAVEN // raven_engine.h
#include "../raven/idlib/TextCompiler.h"
#include "../raven/idlib/math/Radians.h"
// RAVEN BEGIN
// jscott: Effects system interface
#include "../raven/bse/BSEInterface.h"
// RAVEN END

#include "../raven/framework/DeclPlayerModel.h"
// RAVEN BEGIN
// jscott: new decl types
#include "../raven/framework/declLipSync.h"
#include "../raven/framework/declMatType.h"
#include "../raven/framework/declPlayback.h"
// RAVEN END

// Sanity check for any axis in bounds
const float MAX_BOUND_SIZE = 65536.0f;
#endif

#ifdef __ANDROID__ //k: for classic doom
	#ifdef _CDOOM
	#include "../cdoom/Game.h"
	#elif defined _RIVENSIN
	#include "../game/Game.h"
	#elif defined _HARDCORPS
	#include "../game/Game.h"
	#elif defined _QUAKE4
	#include "../quake4/Game.h"
	#elif defined _RAVEN
	#include "../quake4/Game.h"
	#else
	#include "../game/Game.h"
	#endif
#else
	#include "../game/Game.h"
#endif

//-----------------------------------------------------

#ifdef GAME_DLL

#ifdef __ANDROID__ //k: for classic doom
	#ifdef _CDOOM
	#include "../cdoom/Game_local.h"
	#elif defined _RIVENSIN
	#include "../rivensin/Game_local.h"
	#elif defined _HARDCORPS
	#include "../hardcorps/Game_local.h"
	#elif defined _QUAKE4
	#include "../quake4/Game_local.h"
	#elif defined _RAVEN
	#include "../quake4/Game_local.h"
	#else
	#include "../game/Game_local.h"
	#endif
#else
#include "../game/Game_local.h"
#endif

#else

#include "../framework/DemoChecksum.h"

// framework
#include "../framework/Compressor.h"
#include "../framework/EventLoop.h"
#include "../framework/KeyInput.h"
#include "../framework/EditField.h"
#include "../framework/Console.h"
#include "../framework/DemoFile.h"
#include "../framework/Session.h"

// asynchronous networking
#include "../framework/async/AsyncNetwork.h"

// The editor entry points are always declared, but may just be
// stubbed out on non-windows platforms.
#include "../tools/edit_public.h"

// Compilers for map, model, video ehhMathtc. processing.
#include "../tools/compilers/compiler_public.h"

#endif /* !GAME_DLL */

//-----------------------------------------------------

#endif	/* __cplusplus */

#include "idlib/math/prey_math.h"

#endif /* !__PRECOMPILED_H__ */
