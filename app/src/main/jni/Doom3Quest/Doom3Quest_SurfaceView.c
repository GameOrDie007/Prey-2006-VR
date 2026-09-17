/************************************************************************************

Filename	:	Q2VR_SurfaceView.c based on VrCubeWorld_SurfaceView.c
Content		:	This sample uses a plain Android SurfaceView and handles all
				Activity and Surface life cycle events in native code.
Created		:	March, 2015
Authors		:	J.M.P. van Waveren, Simon Brown, Lubos Vonasek

Copyright	:	Copyright 2015 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

#ifdef ANDROID
#include <unistd.h>
#include <pthread.h>
#include <sys/prctl.h>					// for prctl( PR_SET_NAME )
#include <android/log.h>
#include <android/native_window_jni.h>	// for native window JNI
#include <android/input.h>
#endif

#include "VrInput.h"

#ifdef ANDROID
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

// PCVR: gl3.h comes from the vendored Khronos headers on Windows. gl3ext.h is
// not part of that set and never was - it is an Android NDK leftover that
// defines nothing this file uses.
#include <GLES3/gl3.h>
#ifdef ANDROID
#include <GLES3/gl3ext.h>
#endif

#if defined( _WIN32 )
// PCVR: winsock declares a function called shutdown(), and this file has a
// file-local `bool shutdown`. WIN32_LEAN_AND_MEAN keeps winsock out of
// windows.h, but SDL.h below pulls in winsock2.h regardless, so the clash
// survives it. The variable is declared, assigned twice, and referenced
// nowhere else in the project, so renaming it for this translation unit is
// enough and leaves every line of theirs textually untouched.
#define WIN32_LEAN_AND_MEAN
#define shutdown preyvr_shutdown
#include <windows.h>
#include <direct.h>
#include <string.h>
#include "gl_loader.h"
void PCVR_NoteFrame( void );   // sys/win32/win_pcvr.cpp - frame counter
void PCVR_StartWatchdog( void );   // sys/win32/win_pcvr.cpp - the freeze watchdog
void PCVR_FlatRequestDump( int index );       // VrFramebuffer.c - capture the window
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>
#include <SDL2/SDL_mutex.h>

#include "VrInput.h"
#include "VrCommon.h"
#include "VrMath.h"
#include "VrRenderer.h"
#include "PcvrInstrument.h"

// EXT_texture_border_clamp
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER			0x812D
#endif

#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR		0x1004
#endif

vrClientInfo vr;
vrClientInfo *pVRClientInfo;

char **argv;
int argc=0;
bool shutdown;

bool forceVirtualScreen = false;
bool inMenu = true;
bool inGameGuiActive = false;
bool objectiveSystemActive = false;
bool inCinematic = false;
bool loading = false;

void Doom3Quest_setUseScreenLayer(int screen)
{
	inMenu = screen & 0x1;
	inGameGuiActive = !!(screen & 0x2);
	objectiveSystemActive = !!(screen & 0x4);
	inCinematic = !!(screen & 0x8);
	loading = !!(screen & 0x10);

	pVRClientInfo->inMenu = inMenu;
}

#if defined( _WIN32 )
// ---------------------------------------------------------------------------
// PCVR: flatscreen.
//
// "PreyVR.exe -flat" runs the engine with no OpenXR runtime at all - no
// VirtualDesktop, no SteamVR, no headset. It exists because the headless VR
// harness cannot see a picture: under SteamVR's null driver the eye buffer
// comes back pure black, proved with a magenta positive control through the
// same readback. A flat window is the only way to look at what the renderer
// draws without a person wearing something.
//
// It is a runtime branch, not a compile-time fork, so the VR build is the same
// binary and cannot rot. Every VR entry point that touches OpenXR is gated on
// this; the VR_GetConfig/VR_SetConfig pair is just an array and stays in use.
// ---------------------------------------------------------------------------
static bool pcvr_flatscreen = false;
static int  pcvr_flatWidth  = 1280;
static int  pcvr_flatHeight = 720;

// PCVR: the engine derives its whole tick from this - USERCMD_MSEC is
// 1000/GetRefresh() - so at the default 60 a flat run cannot show a frame cost
// below 16.7ms, which is well above the 11.1ms that matters at 90Hz. Settable
// so flat mode can be used to measure render cost against a real budget.
static int  pcvr_flatRefresh = 60;

bool PCVR_Flatscreen( void ) {
	return pcvr_flatscreen;
}

void PCVR_FlatscreenSize( int* width, int* height ) {
	if ( width )  *width  = pcvr_flatWidth;
	if ( height ) *height = pcvr_flatHeight;
}
#endif

bool Doom3Quest_useScreenLayer()
{
#if defined( _WIN32 )
	// PCVR: true means "no per-eye offset". tr_main.cpp gates the IPD shift on
	// this being false, so a flat window wants it true - one centred view. The
	// obvious reading is the wrong one.
	if ( pcvr_flatscreen ) {
		return true;
	}
#endif
	return inMenu || forceVirtualScreen || inCinematic || loading || pVRClientInfo->consoleShown/*Lubos*/;
}

// PCVR: the screen-layer flags as one word, for the freeze report. These five
// are what useScreenLayer() is made of, and which of them is set says which
// transition the renderer was in the middle of when it stopped.
int PCVR_ScreenLayerFlags( void ) {
	int f = 0;

	if ( inMenu )                                   f |= 1;
	if ( forceVirtualScreen )                       f |= 2;
	if ( inCinematic )                              f |= 4;
	if ( loading )                                  f |= 8;
	if ( pVRClientInfo && pVRClientInfo->consoleShown ) f |= 16;
	return f;
}

static void UnEscapeQuotes( char *arg )
{
	char *last = NULL;
	while( *arg ) {
		if( *arg == '"' && *last == '\\' ) {
			char *c_curr = arg;
			char *c_last = last;
			while( *c_curr ) {
				*c_last = *c_curr;
				c_last = c_curr;
				c_curr++;
			}
			*c_last = '\0';
		}
		last = arg;
		arg++;
	}
}

static int ParseCommandLine(char *cmdline, char **argv)
{
	char *bufp;
	char *lastp = NULL;
	int argc, last_argc;
	argc = last_argc = 0;
	for ( bufp = cmdline; *bufp; ) {
		while ( isspace(*bufp) ) {
			++bufp;
		}
		if ( *bufp == '"' ) {
			++bufp;
			if ( *bufp ) {
				if ( argv ) {
					argv[argc] = bufp;
				}
				++argc;
			}
			while ( *bufp && ( *bufp != '"' || *lastp == '\\' ) ) {
				lastp = bufp;
				++bufp;
			}
		} else {
			if ( *bufp ) {
				if ( argv ) {
					argv[argc] = bufp;
				}
				++argc;
			}
			while ( *bufp && ! isspace(*bufp) ) {
				++bufp;
			}
		}
		if ( *bufp ) {
			if ( argv ) {
				*bufp = '\0';
			}
			++bufp;
		}
		if( argv && last_argc != argc ) {
			UnEscapeQuotes( argv[last_argc] );
		}
		last_argc = argc;
	}
	if ( argv ) {
		argv[argc] = NULL;
	}
	return(argc);
}

#ifndef EPSILON
#define EPSILON 0.001f
#endif

static XrVector3f normalizeVec(XrVector3f vec) {
    //NOTE: leave w-component untouched
    float xxyyzz = vec.x*vec.x + vec.y*vec.y + vec.z*vec.z;

	XrVector3f result;
    float invLength = 1.0f / sqrtf(xxyyzz);
    result.x = vec.x * invLength;
    result.y = vec.y * invLength;
    result.z = vec.z * invLength;
    return result;
}

void NormalizeAngles(vec3_t angles)
{
	while (angles[0] >= 90) angles[0] -= 180;
	while (angles[1] >= 180) angles[1] -= 360;
	while (angles[2] >= 180) angles[2] -= 360;
	while (angles[0] < -90) angles[0] += 180;
	while (angles[1] < -180) angles[1] += 360;
	while (angles[2] < -180) angles[2] += 360;
}

void GetAnglesFromVectors(const XrVector3f forward, const XrVector3f right, const XrVector3f up, vec3_t angles)
{
	float sr, sp, sy, cr, cp, cy;

	sp = -forward.z;

	float cp_x_cy = forward.x;
	float cp_x_sy = forward.y;
	float cp_x_sr = -right.z;
	float cp_x_cr = up.z;

	float yaw = atan2(cp_x_sy, cp_x_cy);
	float roll = atan2(cp_x_sr, cp_x_cr);

	cy = cos(yaw);
	sy = sin(yaw);
	cr = cos(roll);
	sr = sin(roll);

	if (fabs(cy) > EPSILON)
	{
	cp = cp_x_cy / cy;
	}
	else if (fabs(sy) > EPSILON)
	{
	cp = cp_x_sy / sy;
	}
	else if (fabs(sr) > EPSILON)
	{
	cp = cp_x_sr / sr;
	}
	else if (fabs(cr) > EPSILON)
	{
	cp = cp_x_cr / cr;
	}
	else
	{
	cp = cos(asin(sp));
	}

	float pitch = atan2(sp, cp);

	angles[0] = pitch / (M_PI*2.f / 360.f);
	angles[1] = yaw / (M_PI*2.f / 360.f);
	angles[2] = roll / (M_PI*2.f / 360.f);

	NormalizeAngles(angles);
}

void QuatToYawPitchRoll(XrQuaternionf q, vec3_t rotation, vec3_t out) {

    ovrMatrix4f mat = ovrMatrix4f_CreateFromQuaternion( &q );

    if (rotation[0] != 0.0f || rotation[1] != 0.0f || rotation[2] != 0.0f)
	{
		ovrMatrix4f rot = ovrMatrix4f_CreateRotation(ToRadians(rotation[0]), ToRadians(rotation[1]), ToRadians(rotation[2]));
		mat = ovrMatrix4f_Multiply(&mat, &rot);
	}

    XrVector4f v1 = {0, 0, -1, 0};
    XrVector4f v2 = {1, 0, 0, 0};
    XrVector4f v3 = {0, 1, 0, 0};

    XrVector4f forwardInVRSpace = XrVector4f_MultiplyMatrix4f((const float *)&mat, &v1);   // PCVR: explicit, C++ will not convert implicitly
    XrVector4f rightInVRSpace = XrVector4f_MultiplyMatrix4f((const float *)&mat, &v2);   // PCVR: explicit, C++ will not convert implicitly
    XrVector4f upInVRSpace = XrVector4f_MultiplyMatrix4f((const float *)&mat, &v3);   // PCVR: explicit, C++ will not convert implicitly

	XrVector3f forward = {-forwardInVRSpace.z, -forwardInVRSpace.x, forwardInVRSpace.y};
	XrVector3f right = {-rightInVRSpace.z, -rightInVRSpace.x, rightInVRSpace.y};
	XrVector3f up = {-upInVRSpace.z, -upInVRSpace.x, upInVRSpace.y};

	XrVector3f forwardNormal = normalizeVec(forward);
	XrVector3f rightNormal = normalizeVec(right);
	XrVector3f upNormal = normalizeVec(up);

	GetAnglesFromVectors(forwardNormal, rightNormal, upNormal, out);
}

/*
========================
Doom3Quest_Vibrate
========================
*/

int Android_GetCVarInteger(const char* cvar);
float Android_GetCVarFloat(const char* cvar);

void Doom3Quest_Vibrate(int channel, float low, float high, int length)
{
#if defined( _WIN32 )
	// PCVR: no session in flat mode, so xrApplyHapticFeedback has nothing to
	// apply to. It was the last thing still reaching a runtime that is not
	// there - four errors in a 30 second run.
	if ( pcvr_flatscreen ) {
		return;
	}
#endif

	if (Android_GetCVarInteger("vr_haptics")) {
		INVR_Vibrate((float)length * 0.001f, channel, low * 0.01f);
	}
}

void jni_haptic_event(const char* event, int position, int flags, int intensity, float angle, float yHeight);
void jni_haptic_updateevent(const char* event, int intensity, float angle);
void jni_haptic_stopevent(const char* event);
void jni_haptic_endframe();
void jni_haptic_enable();
void jni_haptic_disable();

void Doom3Quest_HapticEvent(const char* event, int position, int flags, int intensity, float angle, float yHeight )
{
    jni_haptic_event(event, position, flags, intensity, angle, yHeight);
}

void Doom3Quest_HapticUpdateEvent(const char* event, int intensity, float angle )
{
    jni_haptic_updateevent(event, intensity, angle);
}

void Doom3Quest_HapticEndFrame()
{
	jni_haptic_endframe();
}

void Doom3Quest_HapticStopEvent(const char* event)
{
	jni_haptic_stopevent(event);
}

void Doom3Quest_HapticEnable()
{
    jni_haptic_enable();
}

void Doom3Quest_HapticDisable()
{
    jni_haptic_disable();
}

void VR_Doom3Main(int argc, char** argv);

//Lubos BEGIN
float hmdposition_last[3] = {0};
extern float remote_movementSideways;
extern float remote_movementForward;

void VR_GetMove( float *joy_forward, float *joy_side, float *hmd_forward, float *hmd_side, float *up, float *yaw, float *pitch, float *roll ) {
	float dx = pVRClientInfo->hmdposition_last[0] - hmdposition_last[0];
	float dy = pVRClientInfo->hmdposition_last[1] - hmdposition_last[1];
	float dz = pVRClientInfo->hmdposition_last[2] - hmdposition_last[2];
	if (fabs(dx) + fabs(dy) + fabs(dz) > 1) {
		dx = 0; dy = 0; dz = 0;
	}

	vec2_t v;
	rotateAboutOrigin(dx,-dz, -pVRClientInfo->hmdorientation_temp[YAW], v);
	*hmd_forward = v[0] * 100.0f;
	*hmd_side = v[1] * 100.0f;
	*joy_side = remote_movementSideways;
	*joy_forward = remote_movementForward;
	*up = pVRClientInfo->hmdposition_last[1];

	if (fabs(vr.hmdorientation_diff[PITCH]) > 1) vr.hmdorientation_offset[PITCH] += vr.hmdorientation_diff[PITCH] * 0.1f;
	if (fabs(vr.hmdorientation_diff[ROLL]) > 1) vr.hmdorientation_offset[ROLL] += vr.hmdorientation_diff[ROLL] * 0.1f;
	*pitch = vr.hmdorientation_temp[PITCH] - vr.hmdorientation_offset[PITCH];
	*roll = vr.hmdorientation_temp[ROLL] - vr.hmdorientation_offset[ROLL];
	*yaw = vr.hmdorientation_temp[YAW] + vr.snapTurn;

	hmdposition_last[0] = pVRClientInfo->hmdposition_last[0];
	hmdposition_last[1] = pVRClientInfo->hmdposition_last[1];
	hmdposition_last[2] = pVRClientInfo->hmdposition_last[2];
}

extern XrVector2f pPrimaryJoystick;

void VR_GetJoystick( float *x, float *y ) {
	*x = -pPrimaryJoystick.x;
	*y = -pPrimaryJoystick.y;
}
//Lubos END

static ovrEgl Egl;
#ifdef ANDROID
static ANativeWindow *NativeWindow;
static JavaVM *jVM;
#endif
static bool destroyed = false;

time_t seconds;
int lastRefresh = 0;
int currentRefresh = 60;

float Doom3Quest_GetFOV(int axis)
{
#if defined( _WIN32 )
	if ( pcvr_flatscreen ) {
		// A flat 16:9 window, not a headset's optics. 90 horizontal, and the
		// vertical that the window's own aspect implies.
		const float fovx = 90.0f;
		if ( axis == 0 ) {
			return fovx;
		}
		return fovx * ( (float)pcvr_flatHeight / (float)pcvr_flatWidth );
	}
#endif
	switch (axis) {
		case 0:
			return VR_GetConfigFloat(VR_CONFIG_VIEWPORT_FOVX);
		case 1:
			return VR_GetConfigFloat(VR_CONFIG_VIEWPORT_FOVY);
		default:
			return 0;
	}
}

int Doom3Quest_GetRefresh()
{
#if defined( _WIN32 )
	if ( pcvr_flatscreen ) {
		return pcvr_flatRefresh;
	}
#endif
	return currentRefresh;
}

void Doom3Quest_GetScreenRes(int *width, int *height)
{
#if defined( _WIN32 )
	if ( pcvr_flatscreen ) {
		PCVR_FlatscreenSize( width, height );
		return;
	}
#endif
	VR_GetResolution(VR_GetEngine(), width, height);
}

/*
================
Doom3Quest_InitFrameOnce

VR_InitFrame is xrWaitFrame, and a frame may only wait once. Two call sites on
two threads decide whether to call it, using opposite tests on a flag the game
flips whenever a menu opens, a cinematic starts or a level loads - so on a
transition they can both call it, or neither.

Both calling it is a deadlock with no error attached: the second xrWaitFrame
never returns, the render backend stops, and the main thread waits on it in
BackendThreadWait forever. That is the pause-menu freeze.

So the decision is made once per frame here, by compare-and-swap, and the flag
is cleared in prepareEyeBuffer where xrBeginFrame consumes the wait. Neither
call site's condition is changed - when nothing is flipping, the same side
still does the work at the same moment as before.
================
*/
static volatile long	vr_frameInited = 0;

// Who started each frame, read by the freeze report. Four counters and not one,
// because the previous single "races prevented" counted the rescue call's
// ordinary no-op and so read exactly one per frame - a number that looked like
// a finding and measured nothing at all.
long	pcvr_initBySetup  = 0;   // main thread, the non-screen-layer path
long	pcvr_initByFinish = 0;   // backend, the screen-layer path
long	pcvr_initByRescue = 0;   // neither of the above did it - the real repair
long	pcvr_initSkipped  = 0;   // already started; the ordinary case

static int Doom3Quest_ClaimFrameInit( void ) {
#if defined( _WIN32 )
	return InterlockedCompareExchange( (volatile LONG *)&vr_frameInited, 1, 0 ) == 0;
#else
	return __sync_bool_compare_and_swap( &vr_frameInited, 0, 1 );
#endif
}

static void Doom3Quest_InitFrameOnce( engine_t *engine, long *who ) {
	// Nothing starts a frame before the renderer exists. The swapchain is
	// built in prepareEyeBuffer, which is also where the rescue call sits, so
	// the very first frame of a run is started there and every frame after it
	// on the main thread. Without this the main thread reached VR_InitFrame on
	// frame one, before VR_InitRenderer had ever run.
	if ( !VR_RendererReady( engine ) ) {
		return;
	}

	if ( Doom3Quest_ClaimFrameInit() ) {
		( *who )++;
		VR_InitFrame( engine );
	} else {
		// Already started by the other side. This is the ordinary case for
		// whichever of the two sites is not the owner of the current mode.
		pcvr_initSkipped++;
	}
}

void Doom3Quest_prepareEyeBuffer( )
{
#if defined( _WIN32 )
	if ( pcvr_flatscreen ) {
		if ( !VR_GetConfig( VR_CONFIG_VIEWPORT_VALID ) ) {
			PCVR_FlatRendererInit( &VR_GetEngine()->appState.Renderer.FrameBuffer,
					pcvr_flatWidth, pcvr_flatHeight );
			VR_SetConfig( VR_CONFIG_VIEWPORT_VALID, true );
		}
		PCVR_FlatBind();
		return;
	}
#endif

	// PCVR: first-time creation only. This function runs BETWEEN xrWaitFrame
	// and xrBeginFrame, and VR_InitRenderer destroys the swapchain before it
	// makes a new one - so rebuilding here throws away the buffers of a frame
	// the runtime has already committed to, and the next xrWaitFrame never
	// returns. Measured: hangs at frame 154 with a supersampling change,
	// 2116 clean frames without one. The rebuild now happens in
	// finishEyeBuffer, in the gap where no frame is open.
	//
	// Nothing is in flight the very first time through, because there is no
	// swapchain for a frame to have been waiting on.
	if (!VR_RendererReady(VR_GetEngine())) {
		PCVR_NoteRebuild("prepareEyeBuffer (first creation)");
		VR_InitRenderer(VR_GetEngine(), true);
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, true);
	}

	VR_SetConfigFloat(VR_CONFIG_CANVAS_ASPECT, inMenu ? 1 : 0.85f);
	VR_SetConfigFloat(VR_CONFIG_CANVAS_DISTANCE, 4);
	{
		// PCVR: pcvr_layerMode pins the composition layer so a run can be made
		// without the screen/world switch in it. 0 is the game's own choice and
		// is what ships; 1 and 2 are diagnostic and look wrong on purpose.
		int mode = Doom3Quest_useScreenLayer() ? VR_MODE_STEREO_SCREEN : VR_MODE_STEREO_6DOF;
		const int force = Android_GetCVarInteger("pcvr_layerMode");

		if (force == 1) {
			mode = VR_MODE_STEREO_6DOF;
		} else if (force == 2) {
			mode = VR_MODE_STEREO_SCREEN;
		}

		if (mode != VR_GetConfig(VR_CONFIG_MODE)) {
			PCVR_NoteLayerSwitch(mode == VR_MODE_STEREO_6DOF ? "projection (world)" : "cylinder (screen)");
		}
		VR_SetConfig(VR_CONFIG_MODE, mode);
	}
	VR_SetConfig(VR_CONFIG_PASSTHROUGH, Android_GetCVarInteger("vr_mixedReality"));

	// Whichever side was supposed to start this frame, make sure it happened:
	// on the transition where useScreenLayer() flips the other way, neither of
	// them did, and xrBeginFrame without a preceding xrWaitFrame is the mirror
	// image of the deadlock. A no-op in every ordinary frame.
	Doom3Quest_InitFrameOnce(VR_GetEngine(), &pcvr_initByRescue);

	VR_BeginFrame(VR_GetEngine());

	// xrBeginFrame has consumed the wait, so the next frame needs its own.
	vr_frameInited = 0;

	VR_BindFramebuffer(VR_GetEngine());
}

void Doom3Quest_finishEyeBuffer( )
{
#if defined( _WIN32 )
	// PCVR: offline eye capture, before the frame is handed anywhere. Runs in
	// both modes - in flat mode it is the only way to get the picture out of an
	// unattended run, since nobody is watching the window.
	//
	// pcvr_dumpFrames N writes N samples, one every 600 rendered frames, after
	// the first 300 - far enough in that the map is up and the menu is gone.
	// Deliberately sampled rather than consecutive: consecutive frames of a
	// stalled renderer all look identical and prove nothing.
	//
	// The interval was 60 and that was too tight to be useful. The first sweep
	// caught roadhouse on the 3D Realms intro card and reported it as a black
	// frame; six samples 60 frames apart would all have been the same card. At
	// 600 the samples span a run instead of a moment.
	{
		static int rendered = 0;
		static int written = 0;
		static int armedFor = -1;

		const int want = Android_GetCVarInteger("pcvr_dumpFrames");
		if (want != armedFor) {
			armedFor = want;
			written = 0;
		}

		extern int pcvr_dumpClearTest;
		pcvr_dumpClearTest = Android_GetCVarInteger("pcvr_dumpClearTest");

		rendered++;
		PCVR_NoteFrame();

		if (want > 0 && written < want && rendered > 300 && (rendered % 600) == 0) {
			if ( pcvr_flatscreen ) {
				// Flat mode captures the window instead: the eye texture reads
				// black here while the same texture blits correctly to screen,
				// which made every visual sweep since Milestone 14 report
				// "0.0% lit" for all 25 maps and call it a pass.
				PCVR_FlatRequestDump( written );
			} else {
				PCVR_DumpEyeBuffers(&VR_GetEngine()->appState.Renderer.FrameBuffer, written);
			}
			written++;
		}
	}

	if ( pcvr_flatscreen ) {
		PCVR_FlatPresent();
		return;
	}
#endif

	VR_EndFrame(VR_GetEngine());
	VR_FinishFrame(VR_GetEngine());
	Doom3Quest_HapticEndFrame();

#if defined( _WIN32 )
	// PCVR: the desktop mirror, after the compositor has its copy.
	//
	// Deliberately here and not before xrEndFrame: the headset is served first
	// and the mirror gets whatever time is left, so a slow monitor present
	// cannot delay the frame the player is actually wearing. Timed either way -
	// PCVR_NoteMirror puts it in the run report, because a mirror is exactly
	// the thing that quietly pins a 90 Hz session to 60.
	{
		const int want = Android_GetCVarInteger("pcvr_mirror");

		// Only the blit and the swap here. Showing or hiding the window is a
		// message to the thread that owns it, and this is not that thread -
		// ShowWindow from the backend blocked forever at frame 1, because the
		// pump only runs in flatscreen mode and nothing was draining it.
		// Doom3Quest_FrameSetup does the window half, on the main thread.
		// Paced by the clock, not by the VR frame rate: presenting 90 times a
		// second into a window the desktop can absorb 60 of keeps the swap queue
		// full, and then the blit waits for a vblank. See PCVR_MirrorDue. The
		// check sits outside the timing brackets so a skipped frame costs
		// nothing and is not averaged in as a cheap one.
		if (want > 0 && PCVR_MirrorDue(Android_GetCVarInteger("pcvr_mirrorHz"))) {
			ovrFramebuffer *fb = &VR_GetEngine()->appState.Renderer.FrameBuffer;
			float scale = Android_GetCVarFloat("pcvr_mirrorScale");

			if (scale <= 0.0f) {
				scale = 0.5f;
			}

			// The crop is right for a world view and wrong for a menu. A menu
			// is a fixed panel rather than a view onto something, so cropping
			// it to the window's shape simply cuts the title off the top and
			// the last row off the bottom - which was the reported symptom, and there is
			// no upside to trade against it. The screen layer is exactly the
			// flag for "this is a panel", so show all of it while it is up,
			// whatever the world-view preference is.
			int fit = Android_GetCVarInteger("pcvr_mirrorFit");

			if (Doom3Quest_useScreenLayer()) {
				fit = 3;			// frame the 2D panel, not the whole eye
			}

			PCVR_MirrorBegin();
			PCVR_MirrorPresent(fb,
					Android_GetCVarInteger("pcvr_mirrorEye") ? 1 : 0, scale,
					fit, Android_GetCVarFloat("pcvr_mirrorCropY"));
			PCVR_MirrorEnd();
		}
	}
#endif

	// PCVR: the one moment in the frame with nothing in flight - xrEndFrame has
	// returned and the next xrWaitFrame has not been made yet. Resolution and
	// supersampling changes are applied here for that reason, and nowhere else.
	//
	// FrameSetup clears VR_CONFIG_VIEWPORT_VALID from the main thread whenever
	// supersampling or MSAA changes, which is every time the screen layer goes
	// up or down. Acting on it inside prepareEyeBuffer meant destroying the
	// swapchain of a frame that had already been waited for.
	if (!VR_GetConfig(VR_CONFIG_VIEWPORT_VALID)) {
		PCVR_NoteRebuild("finishEyeBuffer (between frames)");
		VR_InitRenderer(VR_GetEngine(), true);
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, true);
	}

	// Their arrangement, restored. Moving every wait onto the main thread was
	// tried and reverted: the counters showed it never actually took effect -
	// the rescue in prepareEyeBuffer went on doing 152 of 155 waits - and it
	// shipped a build that would not start. Reverted rather than chased,
	// because a working build existed and this took it away.
	if (Doom3Quest_useScreenLayer()) {
		Doom3Quest_InitFrameOnce(VR_GetEngine(), &pcvr_initByFinish);
	}
}

void shutdownVR( void );      // defined just below
void ActivateContext( void );  // ditto - takes the GL context back

// PCVR: called from idCommonLocal::Quit, because the shutdownVR() below it in
// AppThreadFunction is unreachable - VR_Doom3Main never returns. Quit() ends in
// Sys_Quit -> ExitProcess, so without this the OpenXR session is still alive
// when Windows starts running DLL_PROCESS_DETACH handlers, and
// VirtualDesktopXR's does OpenGL work under the loader lock and deadlocks. The
// process then cannot be terminated at all and keeps the exe locked.
//
// Measured on the test machine after a normal quit from the menu: one thread left,
// stack ending
//   PreyVR -> ExitProcess -> LdrShutdownProcess
//     -> virtualdesktop-openxr DllMain -> OPENGL32 -> blocked
//
// Safe to call more than once: VR_LeaveVR checks its own state, and the flag
// below stops a second pass regardless.
void PCVR_ShutdownXR( void ) {
#if defined( _WIN32 )
	static bool done = false;
	if ( done || pcvr_flatscreen ) {
		return;
	}
	done = true;

	// Take the GL context back first. This is not decoration: xrDestroySession
	// inside VirtualDesktopXR makes OpenGL calls, and the context belongs to
	// the render backend thread, so destroying the session from the main
	// thread without this deadlocks - main in glGetError under
	// xrDestroySession, backend in DrvPresentBuffers, neither moving.
	//
	// AppThreadFunction's own tail has always done exactly this, one line
	// before shutdownVR(). It is unreachable because VR_Doom3Main never
	// returns, which is how it came to be left out.
	ActivateContext();

	// End the session before destroying it.
	//
	// VR_LeaveVR calls xrDestroySession straight out, on a session that is
	// still RUNNING. xrEndSession only ever runs from the event handler on
	// XR_SESSION_STATE_STOPPING, and nothing ever asks for that state, so it
	// never arrives. On a Quest the process is being torn down anyway and
	// nobody notices. VirtualDesktopXR does its GL cleanup inside
	// xrDestroySession and blocks there forever:
	//
	//   xrDestroySession -> virtualdesktop-openxr -> OPENGL32 glGetError
	//
	// measured twice, once with the render backend still presenting and once
	// with it fully stopped, so it is the session state and not thread
	// contention.
	//
	// xrRequestExitSession asks the runtime to stop; pumping events then lets
	// the handler call xrEndSession when STOPPING arrives. Bounded, because a
	// shutdown path must not be able to hang - if the runtime never answers,
	// destroy anyway and let the old failure happen rather than a new one.
	{
		engine_t* e = VR_GetEngine();
		if ( e && e->appState.Session != XR_NULL_HANDLE ) {
			printf( "PCVR: asking the runtime to end the session\n" );
			xrRequestExitSession( e->appState.Session );

			int spins = 0;
			while ( e->appState.SessionActive && spins < 200 ) {
				ovrApp_HandleXrEvents( &e->appState );
				Sleep( 10 );
				spins++;
			}
			printf( "PCVR: session %s after %d ms\n",
					e->appState.SessionActive ? "STILL ACTIVE - destroying anyway"
											  : "ended cleanly",
					spins * 10 );
		}
	}

	printf( "PCVR: destroying the OpenXR session before exit\n" );
	shutdownVR();
#endif
}

void shutdownVR() {
	VR_LeaveVR(VR_GetEngine());
}

void jni_shutdown();

#ifdef ANDROID
/* Called before SDL_main() to initialize JNI bindings in SDL library */
extern void SDL_Android_Init(JNIEnv* env, jclass cls);

//Calld on the main thread before the rendering thread is started
void DeactivateContext()
{
    eglMakeCurrent( Egl.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
}

//Caled by the rendering thread to take charge of the context
void ActivateContext()
{
    eglMakeCurrent( Egl.Display, Egl.TinySurface, Egl.TinySurface, Egl.Context );
}
#else

// PCVR: same two entry points, wglMakeCurrent instead of eglMakeCurrent.
// The engine calls these through GLimp_DeactivateContext /
// GLimp_ActivateContext when the backend thread takes over the context,
// which is the same dance their EGL versions do.
void DeactivateContext()
{
	wglMakeCurrent( NULL, NULL );
}

void ActivateContext()
{
	wglMakeCurrent( Egl.Display, Egl.Context );
}
#endif

void * AppThreadFunction(void * parm) {
	//Initialise all our variables
	vr.snapTurn = 0.0f;
	vr.visible_hud = true;

	//init randomiser
	srand(time(NULL));

    pVRClientInfo = &vr;

	// Note that AttachCurrentThread will reset the thread name.
#ifdef ANDROID
	prctl(PR_SET_NAME, (long) "OVR::Main", 0, 0, 0);
#endif

	shutdown = false;

#if defined( _WIN32 )
	vr.pcvr_flatscreen = pcvr_flatscreen ? 1 : 0;
#endif

	ovrEgl_Clear( &Egl );
	ovrEgl_CreateContext(&Egl, NULL);

#ifdef ANDROID
    chdir("/sdcard/PreyVR");
#else
    // PCVR: no equivalent. Sys_DefaultBasePath returns the working
    // directory on Windows, so the game is launched from its own folder
    // and the data lookup rules stay identical to lvonasek's.
#endif

	VR_SetConfig(VR_CONFIG_NEED_RECENTER, true);
#if defined( _WIN32 )
	if ( pcvr_flatscreen ) {
		// No session, no input rig. Show the window that ovrEgl_CreateContext
		// made as a context carrier - it has never been visible until now.
		PCVR_FlatWindowShow( &Egl, pcvr_flatWidth, pcvr_flatHeight );
	} else
#endif
	{
		VR_EnterVR(VR_GetEngine(), Egl);
		IN_VRInit(VR_GetEngine());
	}

    //Should now be all set up and ready - start the Doom3 main loop
    VR_Doom3Main(argc, argv);

    //Take the context back
    ActivateContext();

	//We are done, shutdown cleanly
#if defined( _WIN32 )
	if ( !pcvr_flatscreen )
#endif
	{
		shutdownVR();
	}

	//Ask Java to shut down
    jni_shutdown();

	return NULL;
}

//All the stuff we want to do each frame
void Doom3Quest_FrameSetup(int controlscheme, int switch_sticks, int refresh, float msaa, float supersampling)
{
#if defined( _WIN32 )
	if ( pcvr_flatscreen ) {
		// None of the VR viewport churn applies, and the refresh-rate request
		// needs a session. Everything below this point is skipped.
		return;
	}
#endif
	//Inform GL thread about required framebuffer parameters.
#if !defined( _WIN32 )
	// Their Quest optimisation: render menus, loading screens and cinematics
	// at a higher supersampling than the world, because on a Quest the world
	// is the expensive part and the screen layer is nearly free.
	if (Doom3Quest_useScreenLayer()) {
		supersampling = 1.3f;
	}
#else
	// PCVR: NOT on PC, and this is the single most important line in the port.
	//
	// Changing supersampling clears VR_CONFIG_VIEWPORT_VALID, which makes the
	// renderer rebuild - and VR_InitRenderer destroys the OpenXR swapchain
	// before creating a new one. Doing that while the session has a frame in
	// flight wedges the runtime: nothing fails, no error is returned, and the
	// next xrWaitFrame never comes back. Since the screen layer goes up and
	// down every time a menu opens, a cutscene plays or a level loads, this
	// line made a swapchain teardown happen at exactly those moments - which
	// is every freeze reported so far, on two different OpenXR runtimes.
	//
	// Measured here against SteamVR's null driver: with the supersampling
	// change, a hang in xrWaitFrame at frame 154 of every single run. Without
	// it, 2116 frames and a clean exit.
	//
	// Nothing is lost on PC. The Quest needs the world cheap and the menu
	// sharp; a desktop card can afford one resolution for both, and
	// vr_supersampling now means what it says everywhere.
#endif
	if (fabs(VR_GetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING) - supersampling) > 0.01) {
		VR_SetConfigFloat(VR_CONFIG_VIEWPORT_SUPERSAMPLING, supersampling);
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, false);
	}
#if defined( _WIN32 )
	// PCVR: vr_msaa is ignored here, and not only because MSAA does not exist
	// on this render path.
	//
	// The line below clears VR_CONFIG_VIEWPORT_VALID, and that is the renderer
	// rebuild that destroys the OpenXR swapchain mid-session - the mechanism
	// behind every freeze in this port, written up at length forty lines above
	// for supersampling. So the MSAA row in the VR options menu could wedge the
	// runtime on PC, in exchange for a setting the driver then declines to
	// honour: NVIDIA hands back a non-NULL pointer for the multiview MSAA entry
	// point while not advertising the extension, which is what made the game
	// vanish with vr_msaa 4 (VrFramebuffer.c).
	//
	// Feeding the config's own value back in means the comparison can never
	// fire. The row stays in the menu - it is lvonasek's and on a Quest it
	// works - and on PC it does nothing at all, which is strictly better than
	// doing something harmful.
	msaa = (float)VR_GetConfig(VR_CONFIG_VIEWPORT_MSAA);
#endif
	if (fabs((float)VR_GetConfig(VR_CONFIG_VIEWPORT_MSAA) - msaa) > 0.01) {
		VR_SetConfig(VR_CONFIG_VIEWPORT_MSAA, (int)msaa);
		VR_SetConfig(VR_CONFIG_VIEWPORT_VALID, false);
	}

	//Update refresh rate
	if (lastRefresh != refresh) {
		lastRefresh = refresh;
		VR_SetRefreshRate(refresh);
		currentRefresh = VR_GetRefreshRate();
	}
	// PCVR: the main thread does NOT start VR frames. This is the fix for the
	// freeze, and it is the whole of it.
	//
	// Their arrangement splits xrWaitFrame across two threads by mode - this
	// one while the world is up, the render backend while a menu, cutscene or
	// loading screen is - so a call into the OpenXR session comes from whichever
	// thread happens to own the moment. But only the backend holds the GL
	// context: BackendThread opens with GLimp_ActivateContext, and the main
	// thread hands it over in BackendThreadExecute. A session created with
	// XrGraphicsBindingOpenGLWin32KHR is bound to that context, and calling into
	// it from the thread that gave the context away is what wedges the runtime.
	// Nothing errors; xrWaitFrame just never returns.
	//
	// Measured on SteamVR's null driver, five runs each:
	//
	//     unchanged, waits split across both threads    4 hangs in 5
	//     every wait moved to the main thread           5 hangs in 5
	//     r_multithread 0, one thread for everything    0 hangs in 5
	//     every wait on the backend (this)              0 hangs in 5
	//
	// The two that pass are the two where every OpenXR call is on the context's
	// own thread. r_multithread 0 gets there by having one thread; this gets
	// there while keeping the renderer threaded, which is worth a frame of
	// head-pose latency on the game tic - the rendered view is unaffected,
	// because VR_BeginFrame still poses it on the backend.
	//
	// finishEyeBuffer starts the next frame when the screen layer is up, and the
	// rescue in prepareEyeBuffer covers every other frame. Both are the backend.
	// pcvr_initBySetup must read 0 in any future freeze report.
#if defined( _WIN32 )
	// PCVR: the mirror window, on the thread that owns it. Showing a window is
	// a message to its creator thread, and the render backend is not it.
	if ( !pcvr_flatscreen ) {
		const int want = Android_GetCVarInteger("pcvr_mirror");

		if (want > 0) {
			float scale = Android_GetCVarFloat("pcvr_mirrorScale");
			int w = VR_GetConfig(VR_CONFIG_VIEWPORT_WIDTH);
			int h = VR_GetConfig(VR_CONFIG_VIEWPORT_HEIGHT);

			if (scale <= 0.0f) {
				scale = 0.5f;
			}
			if (w > 0 && h > 0) {
				PCVR_MirrorStart(&Egl, (int)(w * scale), (int)(h * scale));
			}

			// Same thread, same reason. Cheap to call every frame: it returns
			// immediately unless the setting actually changed.
			PCVR_MirrorSetFullscreen(
					!Android_GetCVarInteger("pcvr_mirrorWindowed"));
		} else {
			PCVR_MirrorStop();
		}
	}
#endif

	Doom3Quest_getHMDOrientation();
	pVRClientInfo->right_handed = !controlscheme;
	HandleInput_Default(controlscheme, switch_sticks);
}

void Doom3Quest_getHMDOrientation() {
	// PCVR: the tic is about to build the player's view from this pose. Keep a
	// copy so the projection layer declares the same one, and record which
	// locate it was so the report can prove they match.
	VR_SnapshotGamePose();

	//Don't update game with tracking if we are in big screen mode
    //GB Do pass the stuff but block at my end (if big screen prompt is needed)
	XrPosef hmd = VR_GetView(0);
    const XrQuaternionf quatHmd = hmd.orientation;
    const XrVector3f positionHmd = hmd.position;
    //const XrVector3f translationHmd = tracking->HeadPose.Pose.Translation;
    vec3_t rotation = {0};
    QuatToYawPitchRoll(quatHmd, rotation, vr.hmdorientation_temp);

	VectorSet(vr.hmdposition, positionHmd.x, positionHmd.y, positionHmd.z);
	Vector4Set(vr.hmdorientation_quat, quatHmd.x, quatHmd.y, quatHmd.z, quatHmd.w);
	VectorSubtract(vr.hmdposition_last, vr.hmdposition, vr.hmdposition_delta);
	VectorCopy(vr.hmdposition, vr.hmdposition_last);
}

/*
================================================================================

Activity lifecycle

================================================================================
*/

// PCVR: everything from here to the end of the file is the Android side -
// the Java callbacks for haptics and shutdown, JNI_OnLoad, and the five
// GLES3JNILib entry points that drive the activity lifecycle. None of it has
// a Windows counterpart; the #else below provides what the rest of the file
// and the engine actually call.
#ifdef ANDROID
jmethodID android_shutdown;
jmethodID android_haptic_event;
jmethodID android_haptic_updateevent;
jmethodID android_haptic_stopevent;
jmethodID android_haptic_endframe;
jmethodID android_haptic_enable;
jmethodID android_haptic_disable;
static jobject jniCallbackObj=0;

void jni_shutdown()
{
    ALOGV("Calling: jni_shutdown");
    JNIEnv *env;
    jobject tmp;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
        (*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }
    return (*env)->CallVoidMethod(env, jniCallbackObj, android_shutdown);
}

void jni_haptic_event(const char* event, int position, int flags, int intensity, float angle, float yHeight)
{
    JNIEnv *env;
    jobject tmp;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
        (*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }

    jstring StringArg1 = (*env)->NewStringUTF(env, event);

    return (*env)->CallVoidMethod(env, jniCallbackObj, android_haptic_event, StringArg1, position, flags, intensity, angle, yHeight);
}

void jni_haptic_updateevent(const char* event, int intensity, float angle)
{
    JNIEnv *env;
    jobject tmp;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
        (*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }

    jstring StringArg1 = (*env)->NewStringUTF(env, event);

    return (*env)->CallVoidMethod(env, jniCallbackObj, android_haptic_updateevent, StringArg1, intensity, angle);
}

void jni_haptic_stopevent(const char* event)
{
    ALOGV("Calling: jni_haptic_stopevent");
    JNIEnv *env;
    jobject tmp;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
        (*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }

    jstring StringArg1 = (*env)->NewStringUTF(env, event);

    return (*env)->CallVoidMethod(env, jniCallbackObj, android_haptic_stopevent, StringArg1);
}

void jni_haptic_endframe()
{
    JNIEnv *env;
    jobject tmp;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
        (*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }

    return (*env)->CallVoidMethod(env, jniCallbackObj, android_haptic_endframe);
}

void jni_haptic_enable()
{
    ALOGV("Calling: jni_haptic_enable");
    JNIEnv *env;
    jobject tmp;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
        (*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }

    return (*env)->CallVoidMethod(env, jniCallbackObj, android_haptic_enable);
}

void jni_haptic_disable()
{
    ALOGV("Calling: jni_haptic_disable");
    JNIEnv *env;
    jobject tmp;
    if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
    {
        (*jVM)->AttachCurrentThread(jVM,&env, NULL);
    }

    return (*env)->CallVoidMethod(env, jniCallbackObj, android_haptic_disable);
}

JNIEXPORT jint JNICALL SDL_JNI_OnLoad(JavaVM* vm, void* reserved);

int JNI_OnLoad(JavaVM* vm, void* reserved)
{
	JNIEnv *env;
    jVM = vm;
	if((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK)
	{
		ALOGE("Failed JNI_OnLoad");
		return -1;
	}

	return SDL_JNI_OnLoad(vm, reserved);
}

JNIEXPORT void JNICALL Java_com_lvonasek_preyvr_GLES3JNILib_onCreate( JNIEnv * env, jclass activityClass, jobject activity,
																	   jstring commandLineParams, jstring device)
{
	ALOGV( "    GLES3JNILib::onCreate()" );

	jboolean iscopy;
	const char *arg = (*env)->GetStringUTFChars(env, commandLineParams, &iscopy);

	char *cmdLine = NULL;
	if (arg && strlen(arg))
	{
		cmdLine = strdup(arg);
	}

	(*env)->ReleaseStringUTFChars(env, commandLineParams, arg);

	ALOGV("Command line %s", cmdLine);
	argv = malloc(sizeof(char*) * 255);
	argc = ParseCommandLine(strdup(cmdLine), argv);

	//Get device vendor (uppercase)
	const char *devicename = (*env)->GetStringUTFChars(env, device, &iscopy);
	char vendor[64];
	sscanf(devicename, "%[^:]", vendor);
	for (unsigned int i = 0; i < strlen(vendor); i++) {
		if ((vendor[i] >= 'a') && (vendor[i] <= 'z')) {
			vendor[i] = vendor[i] - 'a' + 'A';
		}
	}

	//Set platform flags
	if (strcmp(vendor, "PLAY FOR DREAM") == 0) {
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_QUEST, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_INSTANCE, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PASSTHROUGH, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PERFORMANCE, true);
	} else if (strcmp(vendor, "PICO") == 0) {
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_PICO, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_INSTANCE, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PASSTHROUGH, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PERFORMANCE, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_REFRESH, true);
	} else {
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_QUEST, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_FOVEATION, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PASSTHROUGH, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_PERFORMANCE, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_REFRESH, true);
		VR_SetPlatformFLag(VR_PLATFORM_VIEWPORT_UNCENTERED, true);
	}
	VR_SetPlatformFLag(VR_PLATFORM_TRACKING_FLOOR, true);

	//Init VR
	ovrJava java;
	java.Vm = (JavaVM*)jVM;
	java.ActivityObject = (*env)->NewGlobalRef( env, activity );
	(*java.Vm)->AttachCurrentThread(java.Vm, &java.Env, NULL);
	VR_Init(&java, "PreyVR", "1");
	VR_InitRenderer(VR_GetEngine(), true);
}

JNIEXPORT void JNICALL Java_com_lvonasek_preyvr_GLES3JNILib_onStart( JNIEnv * env, jobject obj, jobject obj1)
{
	ALOGV( "    GLES3JNILib::onStart()" );

    jniCallbackObj = (jobject)(*env)->NewGlobalRef(env, obj1);
    jclass callbackClass = (*env)->GetObjectClass(env, jniCallbackObj);

    android_shutdown = (*env)->GetMethodID(env,callbackClass,"shutdown","()V");
	android_haptic_event = (*env)->GetMethodID(env, callbackClass, "haptic_event", "(Ljava/lang/String;IIIFF)V");
	android_haptic_updateevent = (*env)->GetMethodID(env, callbackClass, "haptic_updateevent", "(Ljava/lang/String;IF)V");
	android_haptic_stopevent = (*env)->GetMethodID(env, callbackClass, "haptic_stopevent", "(Ljava/lang/String;)V");
	android_haptic_endframe = (*env)->GetMethodID(env, callbackClass, "haptic_endframe", "()V");
    android_haptic_enable = (*env)->GetMethodID(env, callbackClass, "haptic_enable", "()V");
    android_haptic_disable = (*env)->GetMethodID(env, callbackClass, "haptic_disable", "()V");
}

JNIEXPORT void JNICALL Java_com_lvonasek_preyvr_GLES3JNILib_onDestroy( JNIEnv * env, jobject obj )
{
	ALOGV( "    GLES3JNILib::onDestroy()" );
	NativeWindow = NULL;
	destroyed = true;
	shutdown = true;
}

/*
================================================================================

Surface lifecycle

================================================================================
*/

JNIEXPORT void JNICALL Java_com_lvonasek_preyvr_GLES3JNILib_onSurfaceCreated( JNIEnv * env, jobject obj, jobject surface )
{
	ALOGV( "    GLES3JNILib::onSurfaceCreated()" );
	ANativeWindow * newNativeWindow = ANativeWindow_fromSurface( env, surface );
	if ( ANativeWindow_getWidth( newNativeWindow ) < ANativeWindow_getHeight( newNativeWindow ) )
	{
		// An app that is relaunched after pressing the home button gets an initial surface with
		// the wrong orientation even though android:screenOrientation="landscape" is set in the
		// manifest. The choreographer callback will also never be called for this surface because
		// the surface is immediately replaced with a new surface with the correct orientation.
		ALOGE( "        Surface not in landscape mode!" );
	}

	ALOGV( "        NativeWindow = ANativeWindow_fromSurface( env, surface )" );
	if (!NativeWindow) {
		pthread_t thread = 0;
		const int createErr = pthread_create( &thread, NULL, AppThreadFunction, NULL );
		if ( createErr != 0 )
		{
			ALOGE( "pthread_create returned %i", createErr );
		}
	}
	NativeWindow = newNativeWindow;
}

JNIEXPORT void JNICALL Java_com_lvonasek_preyvr_GLES3JNILib_onSurfaceChanged( JNIEnv * env, jobject obj, jobject surface )
{
	ALOGV( "    GLES3JNILib::onSurfaceChanged()" );
	ANativeWindow * newNativeWindow = ANativeWindow_fromSurface( env, surface );
	if ( ANativeWindow_getWidth( newNativeWindow ) < ANativeWindow_getHeight( newNativeWindow ) )
	{
		// An app that is relaunched after pressing the home button gets an initial surface with
		// the wrong orientation even though android:screenOrientation="landscape" is set in the
		// manifest. The choreographer callback will also never be called for this surface because
		// the surface is immediately replaced with a new surface with the correct orientation.
		ALOGE( "        Surface not in landscape mode!" );
	}
	NativeWindow = newNativeWindow;
}

#else

// ---------------------------------------------------------------------------
// PCVR: the Windows side.
//
// On Android the Java activity calls onCreate(), which sets the platform
// flags, calls VR_Init and VR_InitRenderer, then starts a thread running
// AppThreadFunction. Here main() does the same three things in the same order
// and then calls AppThreadFunction directly - there is no activity lifecycle
// to service, so it needs no thread of its own.
// ---------------------------------------------------------------------------

// Their haptics go out to a Java service. There is no equivalent, and
// INVR_Vibrate in VrInput.c already drives xrApplyHapticFeedback directly,
// which is what Doom3Quest_Vibrate calls. These six are the Java-side extras
// only, so they are no-ops rather than reimplementations.
void jni_shutdown() {}
void jni_haptic_event(const char* event, int position, int flags, int intensity, float angle, float yHeight) {}
void jni_haptic_updateevent(const char* event, int intensity, float angle) {}
void jni_haptic_stopevent(const char* event) {}
void jni_haptic_endframe() {}
void jni_haptic_enable() {}
void jni_haptic_disable() {}

void PCVR_InstallCrashHandler( void );   // sys/win32/win_pcvr.cpp

int main(int argc_, char **argv_)
{
	argc = argc_;
	argv = argv_;

	// PCVR: stderr only, and deliberately not stdout.
	//
	// Their ALOGE/ALOGV are printf here, and stdout is where the whole engine
	// log goes. Making *that* unbuffered was a mistake worth recording: several
	// of their log sites sit in the per-frame path, and an unbuffered printf is
	// a synchronous WriteFile, so the render backend thread ended up blocked in
	// ZwWriteFile inside VR_GetResolution once per frame. Against a real Windows
	// console that is catastrophic - the menu ran but the compositor never got a
	// frame, so the headset held the last loading screen.
	//
	// stdout stays block-buffered, which is what it is on any other build. The
	// crash handler flushes it explicitly, and the engine's own `logFile 2`
	// writes qconsole.log through on every Com_Printf, so nothing diagnostic is
	// lost. stderr is unbuffered because the crash handler writes there and must
	// not lose anything if the process is dying.
	setvbuf( stderr, NULL, _IONBF, 0 );

	// PCVR: first thing, before anything can fault. Sys_GetCallStack is a stub
	// on their build because a Quest has nowhere to show a stack, and stdio
	// buffering means a crash also swallows the last few kilobytes of console
	// output. This prints a symbolized stack to stderr and crash.txt instead.
	PCVR_InstallCrashHandler();

	// And the other half of it. The handler above catches a crash, which
	// throws; a freeze throws nothing, so a thread outside the frame loop has
	// to notice that no frame has completed and write down where every thread
	// is stuck. See the watchdog at the bottom of win_pcvr.cpp.
	PCVR_StartWatchdog();

	// Platform flags, matching what their onCreate() sets for a Quest, minus
	// the three extensions VirtualDesktopXR does not advertise. Measured with
	// the Milestone 1 probe:
	//
	//   CONTROLLER_QUEST     kept - VD presents Touch controllers, and this
	//                        selects /interaction_profiles/oculus/touch_controller
	//   TRACKING_FLOOR       kept - same reference space as lvonasek's
	//   VIEWPORT_UNCENTERED  kept - this is a real fovy *= 1.1f in VrRenderer.c,
	//                        not an extension gate, and the optics are the same
	//                        Quest 3 panels either way
	//   EXTENSION_REFRESH    kept - XR_FB_display_refresh_rate is present
	//   FOVEATION            dropped - XR_FB_foveation absent
	//   PASSTHROUGH          dropped - XR_FB_passthrough absent
	//   PERFORMANCE          dropped - XR_EXT_performance_settings absent
	//   INSTANCE             dropped - Android-only extension
	// PCVR: -flat runs with no OpenXR runtime at all. Checked here, before
	// anything asks a runtime for anything.
	for ( int i = 1; i < argc_; i++ ) {
		if ( !argv_[i] ) {
			continue;
		}
		if ( strcmp( argv_[i], "-flat" ) == 0 ) {
			pcvr_flatscreen = true;
			printf( "PCVR: flatscreen - no OpenXR runtime will be used.\n" );
		}
		// -flatres WxH. argv and not a cvar because the window is created in
		// ovrEgl_CreateContext, which runs before the cvar system exists.
		if ( strcmp( argv_[i], "-flatrefresh" ) == 0 && i + 1 < argc_ && argv_[i + 1] ) {
			const int hz = atoi( argv_[i + 1] );
			if ( hz >= 30 && hz <= 240 ) {
				pcvr_flatRefresh = hz;
				printf( "PCVR: flatscreen tick %d Hz\n", hz );
			} else {
				printf( "PCVR: -flatrefresh wants 30..240 - ignoring %s\n", argv_[i + 1] );
			}
		}
		if ( strcmp( argv_[i], "-flatres" ) == 0 && i + 1 < argc_ && argv_[i + 1] ) {
			int w = 0, h = 0;
			if ( sscanf( argv_[i + 1], "%dx%d", &w, &h ) == 2 &&
			     w >= 320 && h >= 240 && w <= 7680 && h <= 4320 ) {
				pcvr_flatWidth = w;
				pcvr_flatHeight = h;
				printf( "PCVR: flatscreen resolution %dx%d\n", w, h );
			} else {
				printf( "PCVR: -flatres wants WxH, e.g. 1920x1080 - ignoring '%s'\n",
						argv_[i + 1] );
			}
		}
	}

	if ( !pcvr_flatscreen ) {
		VR_SetPlatformFLag(VR_PLATFORM_CONTROLLER_QUEST, true);
		VR_SetPlatformFLag(VR_PLATFORM_EXTENSION_REFRESH, true);
		VR_SetPlatformFLag(VR_PLATFORM_VIEWPORT_UNCENTERED, true);
		VR_SetPlatformFLag(VR_PLATFORM_TRACKING_FLOOR, true);

		VR_Init(NULL, "PreyVR", 1);
	}

	// PCVR: their onCreate() calls VR_InitRenderer here too, but it cannot
	// succeed yet - the swapchain and the reference spaces it creates all need
	// a session, and VR_EnterVR does not run until AppThreadFunction below.
	// On Android every one of those calls fails silently, because OXR() is
	// compiled out in release; with PREYVR_XR_CHECKS on it is eight
	// XR_ERROR_HANDLE_INVALID lines in every log before the game even starts.
	//
	// Nothing is lost by leaving it out. VR_CONFIG_VIEWPORT_VALID starts false,
	// so Doom3Quest_prepareEyeBuffer calls VR_InitRenderer on the first frame
	// anyway - which is the call that has always done the real work.

	AppThreadFunction(NULL);
	return 0;
}

#endif