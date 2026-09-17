#pragma once

#ifdef ANDROID
#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "OpenXR", __VA_ARGS__);
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "OpenXR", __VA_ARGS__);
#else
// PCVR: left as stdio.h rather than their <cstdio>. Their Windows path is
// meant to be compiled as C++ - VrInput.c has a _WIN32 branch using
// static_cast, and 42 initialisers here are `= {}` - so <cstdio> was not
// wrong. stdio.h simply works either way, which keeps the option open.
#include <stdio.h>
#define ALOGE(...) printf(__VA_ARGS__)
#define ALOGV(...) printf(__VA_ARGS__)
#endif

#ifdef ANDROID
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#endif

// PCVR: the Windows counterpart of the block above. Desktop OpenXR runtimes
// expose XR_KHR_opengl_enable rather than the ES variant - the Milestone 1
// probe confirmed VirtualDesktopXR advertises it and grants a two-layer array
// swapchain through it - so the binding is the Win32 GL one.
#if defined( _WIN32 )
#include <windows.h>
#include <unknwn.h>          // openxr_platform.h's D3D sections need IUnknown
#define XR_USE_PLATFORM_WIN32 1
#define XR_USE_GRAPHICS_API_OPENGL 1
#endif

#include "openxr.h"
#include "openxr_platform.h"
#include <stdbool.h>

// PCVR: this was unconditional, outside the ANDROID guard above, so the header
// could never have been compiled off Android.
#ifdef ANDROID
#include <jni.h>
#endif

// PCVR: these two guards were _DEBUG only. This port builds Release, so every
// OpenXR and GL call in the VR layer was going completely unchecked - OXR(func)
// and GL(func) both expanded to a bare call. On Android that is invisible
// because development happens in debug builds; here it meant a silent failure
// looked identical to success. PREYVR_XR_CHECKS turns the fork own checking
// back on without dragging in _DEBUG, which would change the CRT and asserts.
#if (defined(_DEBUG) || defined(PREYVR_XR_CHECKS)) && (defined(XR_USE_GRAPHICS_API_OPENGL) || defined(XR_USE_GRAPHICS_API_OPENGL_ES))

void GLCheckErrors(const char* file, int line);

#define GL(func) func; GLCheckErrors(__FILE__ , __LINE__);
#else
#define GL(func) func;
#endif

#if defined(_DEBUG) || defined(PREYVR_XR_CHECKS)
static void OXR_CheckErrors(XrInstance instance, XrResult result, const char* function, bool failOnError) {
	if (XR_FAILED(result)) {
		char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
		xrResultToString(instance, result, errorBuffer);
		if (failOnError) {
			ALOGE("OpenXR error: %s: %s\n", function, errorBuffer);
		} else {
			ALOGV("OpenXR error: %s: %s\n", function, errorBuffer);
		}
	}
}
#define OXR(func) OXR_CheckErrors(VR_GetEngine()->appState.Instance, func, #func, true);
#else
#define OXR(func) func;
#endif

#define DECL_PFN(pfn) PFN_##pfn pfn = NULL
#define INIT_PFN(pfn) OXR(xrGetInstanceProcAddr(engine->appState.Instance, #pfn, (PFN_xrVoidFunction*)(&pfn)))

enum { ovrMaxLayerCount = 3 };
enum { ovrMaxNumEyes = 2 };

typedef union {
	XrCompositionLayerPassthroughFB Passthrough;
	XrCompositionLayerProjection Projection;
	XrCompositionLayerCylinderKHR Cylinder;
#if defined( _WIN32 )
	// PCVR: the screen layer's fallback for a runtime with no cylinder
	// extension. Quad layers are core OpenXR 1.0, so this needs nothing
	// enabled. Windows-only so the Android union's layout is untouched.
	XrCompositionLayerQuad Quad;
#endif
} ovrCompositorLayer_Union;

typedef struct {
	XrSwapchain Handle;
	uint32_t Width;
	uint32_t Height;
} ovrSwapChain;

// PCVR: kept under the same name so VR_EnterVR's signature is unchanged and
// the call sites in Doom3Quest_SurfaceView.c stay as they are. On Windows the
// GL context is a WGL one, so it carries the HDC and HGLRC that
// XrGraphicsBindingOpenGLWin32KHR wants instead of the EGL handles.
#ifdef ANDROID
typedef struct
{
	EGLint		MajorVersion;
	EGLint		MinorVersion;
	EGLDisplay	Display;
	EGLConfig	Config;
	EGLSurface	TinySurface;
	EGLContext	Context;
} ovrEgl;
#else
typedef struct
{
	int		MajorVersion;
	int		MinorVersion;
	HWND	Window;
	HDC		Display;		// hDC   - named to mirror the EGL member it replaces
	HGLRC	Context;		// hGLRC
} ovrEgl;
#endif

typedef struct {
	int Width;
	int Height;
	int Multisamples;
	bool UseMultiview;
	uint32_t TextureSwapChainLength;
	uint32_t TextureSwapChainIndex;
	ovrSwapChain ColorSwapChain;
	// PCVR: XR_KHR_opengl_enable hands back XrSwapchainImageOpenGLKHR. Both
	// structs are {type, next, GLuint image}, so every use of .image below is
	// unaffected - only the type tag differs.
#ifdef ANDROID
	XrSwapchainImageOpenGLESKHR* ColorSwapChainImage;
#else
	XrSwapchainImageOpenGLKHR* ColorSwapChainImage;
#endif
	unsigned int* DepthBuffers;
	unsigned int* FrameBuffers;
} ovrFramebuffer;

typedef struct {
	ovrFramebuffer FrameBuffer;
} ovrRenderer;

typedef struct {
	int Focused;

	XrInstance Instance;
	XrSession Session;
	XrViewConfigurationProperties ViewportConfig;
	XrViewConfigurationView ViewConfigurationView[ovrMaxNumEyes];
	XrSystemId SystemId;
	XrSpace HeadSpace;
	XrSpace StageSpace;
	XrSpace FakeStageSpace;
	XrSpace CurrentSpace;
	int SessionActive;

	int SwapInterval;
	// These threads will be marked as performance threads.
	int MainThreadTid;
	int RenderThreadTid;
	ovrRenderer Renderer;
} ovrApp;

#ifdef ANDROID
typedef struct {
	JavaVM* Vm;
	jobject ActivityObject;
	JNIEnv* Env;
} ovrJava;
#endif

typedef struct {
	uint64_t frameIndex;
	ovrApp appState;
	XrTime predictedDisplayTime;
} engine_t;

enum VRPlatformFlag {
	VR_PLATFORM_CONTROLLER_PICO,
	VR_PLATFORM_CONTROLLER_QUEST,
	VR_PLATFORM_EXTENSION_FOVEATION,
	VR_PLATFORM_EXTENSION_INSTANCE,
	VR_PLATFORM_EXTENSION_PASSTHROUGH,
	VR_PLATFORM_EXTENSION_PERFORMANCE,
	VR_PLATFORM_EXTENSION_REFRESH,
	VR_PLATFORM_TRACKING_FLOOR,
	VR_PLATFORM_VIEWPORT_SQUARE,
	VR_PLATFORM_VIEWPORT_UNCENTERED,
	VR_PLATFORM_MAX
};

void VR_Init( void* system, const char* name, int version );
void VR_Destroy( engine_t* engine );
void VR_EnterVR( engine_t* engine, ovrEgl egl );
void VR_LeaveVR( engine_t* engine );

engine_t* VR_GetEngine( void );
bool VR_GetPlatformFlag(enum VRPlatformFlag flag);
#if defined( _WIN32 )
bool VR_HasCylinderLayer( void );

// PCVR: write both eye layers of the framebuffer to .tga beside the exe. The
// engine renders into a real GL array texture even when the runtime refuses to
// present it, so this reports what the renderer drew with no headset involved.
void PCVR_DumpEyeBuffers( ovrFramebuffer* frameBuffer, int index );

// PCVR: flatscreen - see the block above Doom3Quest_useScreenLayer.
bool PCVR_Flatscreen( void );
void PCVR_FlatscreenSize( int* width, int* height );
void PCVR_FlatWindowShow( ovrEgl* egl, int width, int height );
void PCVR_FlatRendererInit( ovrFramebuffer* frameBuffer, int width, int height );
void PCVR_FlatBind( void );
void PCVR_FlatPresent( void );

// PCVR: the desktop mirror, for streaming. Off unless pcvr_mirror says so.
void PCVR_MirrorStart( ovrEgl* egl, int width, int height );
void PCVR_MirrorPresent( ovrFramebuffer* fb, int eye, float scale, int fit, float cropY );
int  PCVR_MirrorDue( int hz );
void PCVR_ApplySwapInterval( void );
void PCVR_MirrorStop( void );
void PCVR_MirrorSetFullscreen( int on );
#endif
void VR_SetPlatformFLag(enum VRPlatformFlag flag, bool value);

void ovrApp_Clear(ovrApp* app);
void ovrApp_Destroy(ovrApp* app);
int ovrApp_HandleXrEvents(ovrApp* app);
