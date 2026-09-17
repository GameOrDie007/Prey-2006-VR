#include "VrFramebuffer.h"
#include "PcvrInstrument.h"
// PCVR: FromXrTime is declared here. C accepted it as an implicit
// declaration; C++ does not.
#include "VrMath.h"

// PCVR: they already guarded pthread and gl3.h off Windows here, but left
// SDL_opengl.h unguarded - and on Windows that pulls in desktop <GL/gl.h>,
// whose GL 1.1 declarations carry dllimport and collide with the vendored
// Khronos ES headers under KHRONOS_STATIC. Windows takes gl3.h and the
// loader instead.
#if defined(_WIN32)
#include <GLES3/gl3.h>
#include "gl_loader.h"

// PCVR: eglGetProcAddress has no Windows counterpart. PCVR_GL_GetProc is
// wglGetProcAddress with the opengl32.dll fallback, so alias it rather than
// editing their four call sites in ovrFramebuffer_Create.
#define eglGetProcAddress PCVR_GL_GetProc

// PCVR: wglGetProcAddress is not a support test.
//
// On Android, eglGetProcAddress returns NULL for an entry point the driver
// does not implement, so ovrFramebuffer_Create can decide whether to take its
// multisampled-multiview path purely on a NULL check. On Windows the driver
// hands back a valid-looking pointer for any symbol it knows of, advertised or
// not. Measured on NVIDIA 610.88:
//
//   GL_OVR_multiview_multisampled_render_to_texture   NOT advertised
//   glFramebufferTextureMultisampleMultiviewOVR       returns 0x7ffe397af460
//
// Calling it fails inside nvoglv64.dll as STATUS_STACK_BUFFER_OVERRUN
// (0xc0000409), which is a fail-fast and so bypasses the crash handler
// entirely - the game simply vanished, with vr_msaa 4 and not with vr_msaa 0.
//
// So on Windows the extension string is checked as well, and the pointers are
// cleared when it is absent. Their existing NULL guards then fall through to
// glFramebufferTextureMultiviewOVR, which GL_OVR_multiview2 does advertise:
// rendering works, without MSAA.
static int PCVR_HasGLExtension( const char *want ) {
	typedef const GLubyte * (GL_APIENTRYP PFN_GETSTRINGI)( GLenum, GLuint );
	static PFN_GETSTRINGI pfnGetStringi = NULL;
	GLint count = 0;
	GLint i;

	if ( !pfnGetStringi ) {
		pfnGetStringi = (PFN_GETSTRINGI)PCVR_GL_GetProc( "glGetStringi" );
	}

	if ( !pfnGetStringi ) {
		return 0;
	}

	glGetIntegerv( GL_NUM_EXTENSIONS, &count );

	for ( i = 0; i < count; i++ ) {
		const GLubyte *e = pfnGetStringi( GL_EXTENSIONS, (GLuint)i );
		if ( e && strcmp( (const char *)e, want ) == 0 ) {
			return 1;
		}
	}

	return 0;
}

// These three are not in ES 3.0. GL_CLAMP_TO_BORDER and GL_TEXTURE_BORDER_COLOR
// come from EXT_texture_border_clamp on Android - Doom3Quest_SurfaceView.c
// defines exactly these two the same way. GL_FRAMEBUFFER_SRGB is desktop GL,
// where sRGB conversion is an explicit enable rather than implicit.
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER      0x812D
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB     0x8DB9
#endif
#else
#include "SDL_opengl.h"
#include <pthread.h>
#include <GLES3/gl3.h>

#endif

void GLCheckErrors(const char* file, int line) {
    for (int i = 0; i < 10; i++) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        ALOGE("GL error on line %s:%d %d", file, line, error);
    }
}

typedef void (GL_APIENTRYP PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)(
        GLenum target,
        GLsizei samples,
        GLenum internalformat,
        GLsizei width,
        GLsizei height);
typedef void (GL_APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)(
        GLenum target,
        GLenum attachment,
        GLenum textarget,
        GLuint texture,
        GLint level,
        GLsizei samples);

typedef void(GL_APIENTRY* PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(
		GLenum target,
		GLenum attachment,
		GLuint texture,
		GLint level,
		GLint baseViewIndex,
		GLsizei numViews);

typedef void(GL_APIENTRY* PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(
        GLenum target,
        GLenum attachment,
        GLuint texture,
        GLint level,
        GLsizei samples,
        GLint baseViewIndex,
        GLsizei numViews);

/*
================================================================================

ovrEgl

================================================================================
*/

// PCVR: everything from here to the end of ovrEgl_CreateContext is EGL. The
// Windows equivalent follows in the #else - same two function names and
// signatures, so AppThreadFunction calls them unchanged.
#ifdef ANDROID
void ovrEgl_Clear( ovrEgl * egl )
{
	egl->MajorVersion = 0;
	egl->MinorVersion = 0;
	egl->Display = 0;
	egl->Config = 0;
	egl->TinySurface = EGL_NO_SURFACE;
	egl->Context = EGL_NO_CONTEXT;
}

void ovrEgl_CreateContext( ovrEgl * egl, const ovrEgl * shareEgl )
{
	if ( egl->Display != 0 )
	{
		return;
	}

	egl->Display = eglGetDisplay( EGL_DEFAULT_DISPLAY );
	eglInitialize( egl->Display, &egl->MajorVersion, &egl->MinorVersion );
	// Do NOT use eglChooseConfig, because the Android EGL code pushes in multisample
	// flags in eglChooseConfig if the user has selected the "force 4x MSAA" option in
	// settings, and that is completely wasted for our warp target.
	const int MAX_CONFIGS = 1024;
	EGLConfig configs[MAX_CONFIGS];
	EGLint numConfigs = 0;
	if ( eglGetConfigs( egl->Display, configs, MAX_CONFIGS, &numConfigs ) == EGL_FALSE )
	{
		ALOGE( "        eglGetConfigs() failed: %d", eglGetError() );
		return;
	}
	const EGLint configAttribs[] =
			{
					EGL_RED_SIZE,		8,
					EGL_GREEN_SIZE,		8,
					EGL_BLUE_SIZE,		8,
					EGL_ALPHA_SIZE,		8, // need alpha for the multi-pass timewarp compositor
					EGL_DEPTH_SIZE,		24,
					EGL_STENCIL_SIZE,	8,
					EGL_SAMPLES,		0,
					EGL_NONE
			};
	egl->Config = 0;
	for ( int i = 0; i < numConfigs; i++ )
	{
		EGLint value = 0;

		eglGetConfigAttrib( egl->Display, configs[i], EGL_RENDERABLE_TYPE, &value );
		if ( ( value & EGL_OPENGL_ES3_BIT_KHR ) != EGL_OPENGL_ES3_BIT_KHR )
		{
			continue;
		}

		// The pbuffer config also needs to be compatible with normal window rendering
		// so it can share textures with the window context.
		eglGetConfigAttrib( egl->Display, configs[i], EGL_SURFACE_TYPE, &value );
		if ( ( value & ( EGL_WINDOW_BIT | EGL_PBUFFER_BIT ) ) != ( EGL_WINDOW_BIT | EGL_PBUFFER_BIT ) )
		{
			continue;
		}

		int	j = 0;
		for ( ; configAttribs[j] != EGL_NONE; j += 2 )
		{
			eglGetConfigAttrib( egl->Display, configs[i], configAttribs[j], &value );
			if ( value != configAttribs[j + 1] )
			{
				break;
			}
		}
		if ( configAttribs[j] == EGL_NONE )
		{
			egl->Config = configs[i];
			break;
		}
	}
	if ( egl->Config == 0 )
	{
		ALOGE( "        eglChooseConfig() failed: %d", eglGetError() );
		return;
	}
	EGLint contextAttribs[] =
			{
					EGL_CONTEXT_CLIENT_VERSION, 3,
					EGL_NONE
			};
	ALOGV( "        Context = eglCreateContext( Display, Config, EGL_NO_CONTEXT, contextAttribs )" );
	egl->Context = eglCreateContext( egl->Display, egl->Config, ( shareEgl != NULL ) ? shareEgl->Context : EGL_NO_CONTEXT, contextAttribs );
	if ( egl->Context == EGL_NO_CONTEXT )
	{
		ALOGE( "        eglCreateContext() failed: %d", eglGetError() );
		return;
	}
	const EGLint surfaceAttribs[] =
			{
					EGL_WIDTH, 16,
					EGL_HEIGHT, 16,
					EGL_NONE
			};
	ALOGV( "        TinySurface = eglCreatePbufferSurface( Display, Config, surfaceAttribs )" );
	egl->TinySurface = eglCreatePbufferSurface( egl->Display, egl->Config, surfaceAttribs );
	if ( egl->TinySurface == EGL_NO_SURFACE )
	{
		ALOGE( "        eglCreatePbufferSurface() failed: %d", eglGetError() );
		eglDestroyContext( egl->Display, egl->Context );
		egl->Context = EGL_NO_CONTEXT;
		return;
	}
	ALOGV( "        eglMakeCurrent( Display, TinySurface, TinySurface, Context )" );
	if ( eglMakeCurrent( egl->Display, egl->TinySurface, egl->TinySurface, egl->Context ) == EGL_FALSE )
	{
		ALOGE( "        eglMakeCurrent() failed: %d", eglGetError() );
		eglDestroySurface( egl->Display, egl->TinySurface );
		eglDestroyContext( egl->Display, egl->Context );
		egl->Context = EGL_NO_CONTEXT;
		return;
	}
}
#else

// PCVR: the WGL half. Theirs makes an EGL display, a 1x1 pbuffer and a
// context, then makes it current; this does the same with a hidden window,
// its HDC and an HGLRC. VR_EnterVR reads Display and Context out of the
// struct for XrGraphicsBindingOpenGLWin32KHR.
//
// The Milestone 1 probe created a VDXR session from a context built exactly
// this way, so the pixel format below is known to be acceptable.

void ovrEgl_Clear( ovrEgl * egl )
{
	egl->MajorVersion = 0;
	egl->MinorVersion = 0;
	egl->Window = NULL;
	egl->Display = NULL;
	egl->Context = NULL;
}

// PCVR: a fully transparent cursor, for this window's class.
//
// The class used to register hCursor as NULL, which does NOT mean "no cursor".
// It means the window never sets one, so the pointer keeps whatever it was when
// the process started - on Windows 11 that is IDC_APPSTARTING, the spinning
// blue ring that means "busy". It sat over the desktop mirror looking exactly
// like a hung application.
//
// Blank rather than IDC_ARROW because the mirror is a video feed of something
// being played in a headset; a pointer on it is noise. The cursor outside this
// window is the system's own and is not touched.
static HCURSOR PCVR_BlankCursor( void ) {
	static HCURSOR blank = NULL;
	static int tried = 0;

	if ( !tried ) {
		// 32x32 at one bit per pixel. AND all ones with XOR all zeros is
		// "leave the screen exactly as it is", which is an invisible cursor.
		BYTE andMask[32 * 32 / 8];
		BYTE xorMask[32 * 32 / 8];

		tried = 1;
		memset( andMask, 0xFF, sizeof( andMask ) );
		memset( xorMask, 0x00, sizeof( xorMask ) );
		blank = CreateCursor( GetModuleHandleA( NULL ), 0, 0, 32, 32,
				andMask, xorMask );
	}

	// If the driver or the OS refuses, an arrow is still far better than
	// leaving it on the spinning ring.
	return blank ? blank : LoadCursorA( NULL, (LPCSTR)IDC_ARROW );
}

void ovrEgl_CreateContext( ovrEgl * egl, const ovrEgl * shareEgl )
{
	if ( egl->Display != NULL )
	{
		return;
	}

	// PCVR: both modes need a real window procedure, or the keyboard is dropped.
	//
	// This used to be PCVR_FlatWndProc in flat mode and DefWindowProcA in VR,
	// on the grounds that the VR window was a context carrier that was never
	// shown and so could never be typed into. The desktop mirror shows it now,
	// fullscreen and by default, which makes it the foreground window for the
	// whole session - so every key the player pressed went to DefWindowProcA
	// and was thrown away. The console would not open and no bind worked.
	//
	// The procedure itself keeps the mouse behind PCVR_Flatscreen(): in VR the
	// pointer belongs to the desktop.
	extern LRESULT CALLBACK PCVR_FlatWndProc( HWND, UINT, WPARAM, LPARAM );

	WNDCLASSA wc;
	memset( &wc, 0, sizeof( wc ) );
	wc.lpfnWndProc = PCVR_FlatWndProc;
	wc.hInstance = GetModuleHandleA( NULL );
	wc.lpszClassName = "PreyVR";
	wc.hCursor = PCVR_BlankCursor();
	RegisterClassA( &wc );

	egl->Window = CreateWindowExA( 0, "PreyVR", "PreyVR", WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, 640, 360, NULL, NULL, wc.hInstance, NULL );
	if ( !egl->Window )
	{
		ALOGE( "        CreateWindowEx() failed: %lu", GetLastError() );
		return;
	}

	egl->Display = GetDC( egl->Window );

	PIXELFORMATDESCRIPTOR pfd;
	memset( &pfd, 0, sizeof( pfd ) );
	pfd.nSize = sizeof( pfd );
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;

	int pf = ChoosePixelFormat( egl->Display, &pfd );
	if ( !pf || !SetPixelFormat( egl->Display, pf, &pfd ) )
	{
		ALOGE( "        SetPixelFormat() failed: %lu", GetLastError() );
		return;
	}

	egl->Context = wglCreateContext( egl->Display );
	if ( !egl->Context )
	{
		ALOGE( "        wglCreateContext() failed: %lu", GetLastError() );
		return;
	}

	if ( !wglMakeCurrent( egl->Display, egl->Context ) )
	{
		ALOGE( "        wglMakeCurrent() failed: %lu", GetLastError() );
		wglDeleteContext( egl->Context );
		egl->Context = NULL;
		return;
	}

	egl->MajorVersion = 4;
	egl->MinorVersion = 6;

	// Resolve the GL entry points opengl32.dll does not export, now that a
	// context is current. GLimp_Init calls this again; it is idempotent.
	PCVR_GL_Init();
}

#endif

/*
================================================================================

ovrFramebuffer

================================================================================
*/

void ovrFramebuffer_Clear(ovrFramebuffer* frameBuffer) {
	frameBuffer->Width = 0;
	frameBuffer->Height = 0;
	frameBuffer->Multisamples = 0;
	frameBuffer->TextureSwapChainLength = 0;
	frameBuffer->TextureSwapChainIndex = 0;
	frameBuffer->ColorSwapChain.Handle = XR_NULL_HANDLE;
	frameBuffer->ColorSwapChain.Width = 0;
	frameBuffer->ColorSwapChain.Height = 0;
	frameBuffer->ColorSwapChainImage = NULL;
	frameBuffer->DepthBuffers = NULL;
	frameBuffer->FrameBuffers = NULL;
}

#if defined( _WIN32 )

// ---------------------------------------------------------------------------
// PCVR: offline eye capture.
//
// Under a headless runtime the frame loop's xrBeginFrame/xrEndFrame fail, but
// ovrFramebuffer_SetCurrent still binds a real FBO over a real array texture,
// so the engine renders a real picture into it - the presentation is what is
// missing, not the drawing. Reading those two layers back is therefore a
// truthful answer to "what is the renderer producing", available with nothing
// on anyone's head.
//
// A statistic would not be: a mean pixel value cannot tell a bad scene from a
// bad render. This writes the buffer out and lets a person look at it.
// ---------------------------------------------------------------------------

typedef void (GL_APIENTRY *PFN_PCVR_glFramebufferTextureLayer)(
		GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);

typedef void (GL_APIENTRY *PFN_PCVR_glBlitFramebuffer)(
		GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
		GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
		GLbitfield mask, GLenum filter);

static void PCVR_WriteTGA( const char* path, const unsigned char* bgra, int w, int h ) {
	FILE* f = fopen( path, "wb" );
	if ( !f ) {
		ALOGE( "PCVR: could not open %s for writing\n", path );
		return;
	}

	unsigned char header[18];
	memset( header, 0, sizeof( header ) );
	header[2]  = 2;                       // uncompressed true-colour
	header[12] = (unsigned char)( w & 0xFF );
	header[13] = (unsigned char)( ( w >> 8 ) & 0xFF );
	header[14] = (unsigned char)( h & 0xFF );
	header[15] = (unsigned char)( ( h >> 8 ) & 0xFF );
	header[16] = 32;                      // bits per pixel
	header[17] = 8;                       // 8 bits of alpha, origin bottom-left

	fwrite( header, 1, sizeof( header ), f );
	fwrite( bgra, 1, (size_t)w * (size_t)h * 4, f );
	fclose( f );
}

// PCVR: set by Doom3Quest_finishEyeBuffer from pcvr_dumpClearTest.
//
// A black eye dump is not evidence on its own - a broken readback and an empty
// framebuffer look identical. With this on, the attachment is cleared to
// magenta immediately before glReadPixels, so the file that comes out says
// which of the two is true. It destroys the frame it tests, which is the point:
// a control that cannot change the picture is not a control.
int pcvr_dumpClearTest = 0;

void PCVR_DumpEyeBuffers( ovrFramebuffer* frameBuffer, int index ) {
	if ( !frameBuffer || !frameBuffer->ColorSwapChainImage ) {
		return;
	}

	static PFN_PCVR_glFramebufferTextureLayer p_glFramebufferTextureLayer = NULL;
	if ( !p_glFramebufferTextureLayer ) {
		p_glFramebufferTextureLayer = (PFN_PCVR_glFramebufferTextureLayer)
				PCVR_GL_GetProc( "glFramebufferTextureLayer" );
		if ( !p_glFramebufferTextureLayer ) {
			ALOGE( "PCVR: no glFramebufferTextureLayer; cannot dump eyes\n" );
			return;
		}
	}

	const int w = (int)frameBuffer->ColorSwapChain.Width;
	const int h = (int)frameBuffer->ColorSwapChain.Height;
	const GLuint tex = frameBuffer->ColorSwapChainImage[frameBuffer->TextureSwapChainIndex].image;
	const int layers = frameBuffer->UseMultiview ? 2 : 1;

	unsigned char* pixels = (unsigned char*)malloc( (size_t)w * (size_t)h * 4 );
	if ( !pixels ) {
		ALOGE( "PCVR: out of memory dumping a %dx%d eye\n", w, h );
		return;
	}

	GLint previous = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previous );

	GLuint fbo = 0;
	glGenFramebuffers( 1, &fbo );

	for ( int layer = 0; layer < layers; layer++ ) {
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		p_glFramebufferTextureLayer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, layer );

		const GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
		if ( status != GL_FRAMEBUFFER_COMPLETE ) {
			ALOGE( "PCVR: eye dump FBO incomplete for layer %d (0x%04X)\n", layer, status );
			continue;
		}

		if ( pcvr_dumpClearTest ) {
			glClearColor( 1.0f, 0.0f, 1.0f, 1.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			ALOGE( "PCVR: dump control ON - layer %d cleared to magenta before read\n", layer );
		}

		glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels );

		// GL hands back RGBA; TGA wants BGRA. Swap in place.
		for ( size_t i = 0; i < (size_t)w * (size_t)h * 4; i += 4 ) {
			const unsigned char r = pixels[i];
			pixels[i] = pixels[i + 2];
			pixels[i + 2] = r;
		}

		char path[512];
		sprintf( path, "eyedump_%04d_%s.tga", index, layer == 0 ? "L" : "R" );
		PCVR_WriteTGA( path, pixels, w, h );
		ALOGE( "PCVR: wrote %s (%dx%d)\n", path, w, h );
	}

	glDeleteFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, (GLuint)previous );
	free( pixels );
}

#endif

#if defined( _WIN32 )

// ---------------------------------------------------------------------------
// PCVR: flatscreen presentation.
//
// ovrEgl_CreateContext already makes a real Win32 window and a WGL context - it
// has only ever been a context carrier, 640x360 and never shown. Flat mode
// sizes it, shows it, and presents to it.
// ---------------------------------------------------------------------------
static HDC  pcvr_flatDC  = NULL;
static HWND pcvr_flatHWND = NULL;

void PCVR_FlatWindowShow( ovrEgl* egl, int width, int height ) {
	if ( !egl || !egl->Window ) {
		ALOGE( "PCVR: flatscreen has no window to show\n" );
		return;
	}

	pcvr_flatHWND = egl->Window;
	pcvr_flatDC   = egl->Display;

	// Size the client area, not the frame: AdjustWindowRect turns the one into
	// the other, or the viewport ends up smaller than asked for by exactly the
	// border and caption.
	RECT r;
	r.left = 0;
	r.top = 0;
	r.right = width;
	r.bottom = height;
	AdjustWindowRect( &r, WS_OVERLAPPEDWINDOW, FALSE );

	SetWindowTextA( pcvr_flatHWND, "PreyVR (flatscreen)" );
	SetWindowPos( pcvr_flatHWND, NULL, 0, 0,
			r.right - r.left, r.bottom - r.top,
			SWP_NOMOVE | SWP_NOZORDER );
	ShowWindow( pcvr_flatHWND, SW_SHOW );
	UpdateWindow( pcvr_flatHWND );

	// Vsync is turned off in PCVR_ApplySwapInterval, called from the present
	// path, NOT here.
	//
	// It used to be here, and it never once worked: this function runs on the
	// main thread, wglGetProcAddress resolves against the current GL context,
	// and only the render backend holds one. The lookup returned NULL every
	// run and the "flatscreen vsync off" line beside it was never printed -
	// which nobody noticed, because ALOGE is printf and this is a GUI app.
	//
	// The original note is still worth keeping: two full rounds of settings
	// measurements were taken before the cap was spotted at all. Both reported
	// median 17ms, p95 17ms and p99 17ms for every case - identical percentiles
	// are a cap, not a distribution, and 17ms is a 60 Hz monitor.
}

// The framebuffer flat mode renders into. Owned by the engine's renderer
// struct, which exists whether or not a session ever did.
static ovrFramebuffer* pcvr_flatFB = NULL;
static GLuint pcvr_flatBlitFBO = 0;

// Defined further down this file; flat mode calls it with a null session.
bool ovrFramebuffer_Create( XrSession session, ovrFramebuffer* frameBuffer,
		const bool useMultiview, const int width, const int height, int multisamples );

void PCVR_FlatRendererInit( ovrFramebuffer* frameBuffer, int width, int height ) {
	// multisamples 1: MSAA does not exist on this platform anyway - the
	// multiview multisample extension is absent and asking for it crashes the
	// NVIDIA driver. See Milestone 12.
	if ( !ovrFramebuffer_Create( XR_NULL_HANDLE, frameBuffer, true, width, height, 1 ) ) {
		ALOGE( "PCVR: flatscreen framebuffer creation failed\n" );
		return;
	}
	pcvr_flatFB = frameBuffer;
	ALOGE( "PCVR: flatscreen renderer ready, %dx%d\n", width, height );
}

void PCVR_FlatBind( void ) {
	if ( pcvr_flatFB ) {
		ovrFramebuffer_SetCurrent( pcvr_flatFB );
	}
}

// PCVR: how many window captures are still owed, and the next file number.
// Set by PCVR_FlatRequestDump so the capture happens inside the present, where
// the back buffer holds the finished picture.
static int	pcvr_flatDumpsWanted = 0;
static int	pcvr_flatDumpIndex = 0;

void PCVR_FlatRequestDump( int index ) {
	pcvr_flatDumpsWanted = 1;
	pcvr_flatDumpIndex = index;
}

// Write the window's back buffer to a TGA. Called after the blit and before
// SwapBuffers, so this is exactly the image that is about to be shown.
//
// The eye-texture path (PCVR_DumpEyeBuffers) reads black in flat mode even
// though its own magenta control passes and the blit of the same texture is
// correct on screen. This does not try to explain that - it reads the buffer
// that is known to hold the picture.
static void PCVR_FlatDumpWindow( int index ) {
	int w = 0, h = 0;
	PCVR_FlatscreenSize( &w, &h );
	if ( w <= 0 || h <= 0 ) {
		return;
	}

	unsigned char* pixels = (unsigned char*)malloc( (size_t)w * (size_t)h * 4 );
	if ( !pixels ) {
		return;
	}

	glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	glReadBuffer( GL_BACK );
	glReadPixels( 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels );

	for ( size_t i = 0; i < (size_t)w * (size_t)h * 4; i += 4 ) {
		const unsigned char r = pixels[i];
		pixels[i] = pixels[i + 2];
		pixels[i + 2] = r;
	}

	char name[ 256 ];
	sprintf( name, "eyedump_%04d_FLAT.tga", index );

	FILE* f = fopen( name, "wb" );
	if ( f ) {
		unsigned char header[ 18 ];
		memset( header, 0, sizeof( header ) );
		header[ 2 ] = 2;
		header[ 12 ] = (unsigned char)( w & 0xFF );
		header[ 13 ] = (unsigned char)( ( w >> 8 ) & 0xFF );
		header[ 14 ] = (unsigned char)( h & 0xFF );
		header[ 15 ] = (unsigned char)( ( h >> 8 ) & 0xFF );
		header[ 16 ] = 32;
		fwrite( header, 1, sizeof( header ), f );
		fwrite( pixels, 1, (size_t)w * (size_t)h * 4, f );
		fclose( f );
		ALOGE( "PCVR: wrote %s (%dx%d, from the window back buffer)\n", name, w, h );
	}

	free( pixels );
}

void PCVR_FlatPresent( void ) {
	if ( !pcvr_flatDC ) {
		return;
	}

	// Blit layer 0 - the left eye - to the window. One centred view, because
	// Doom3Quest_useScreenLayer() is true in flat mode and the per-eye offset
	// never runs, so both layers hold the same image.
	if ( pcvr_flatFB && pcvr_flatFB->ColorSwapChainImage ) {
		static PFN_PCVR_glFramebufferTextureLayer p_layer = NULL;
		static PFN_PCVR_glBlitFramebuffer p_blit = NULL;
		if ( !p_layer ) {
			p_layer = (PFN_PCVR_glFramebufferTextureLayer)
					PCVR_GL_GetProc( "glFramebufferTextureLayer" );
		}
		if ( !p_blit ) {
			p_blit = (PFN_PCVR_glBlitFramebuffer)
					PCVR_GL_GetProc( "glBlitFramebuffer" );
		}

		if ( p_layer && p_blit ) {
			if ( !pcvr_flatBlitFBO ) {
				glGenFramebuffers( 1, &pcvr_flatBlitFBO );
			}

			const GLuint tex =
					pcvr_flatFB->ColorSwapChainImage[pcvr_flatFB->TextureSwapChainIndex].image;
			const int sw = (int)pcvr_flatFB->ColorSwapChain.Width;
			const int sh = (int)pcvr_flatFB->ColorSwapChain.Height;

			int dw = sw, dh = sh;
			PCVR_FlatscreenSize( &dw, &dh );

			glBindFramebuffer( GL_READ_FRAMEBUFFER, pcvr_flatBlitFBO );
			p_layer( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, 0 );
			glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );

			// The engine renders with GL_FRAMEBUFFER_SRGB off, writing linear
			// values into an sRGB texture. Reading it back through a blit would
			// apply the decode it was never encoded with, so the flag stays off
			// across the blit too and the bytes are moved, not reinterpreted.
			glDisable( GL_FRAMEBUFFER_SRGB );
			p_blit( 0, 0, sw, sh, 0, 0, dw, dh,
					GL_COLOR_BUFFER_BIT, GL_LINEAR );

			glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
			glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
		}
	}

	// After the blit, before the swap: the back buffer now holds exactly what
	// is about to be shown.
	if ( pcvr_flatDumpsWanted > 0 ) {
		pcvr_flatDumpsWanted = 0;
		PCVR_FlatDumpWindow( pcvr_flatDumpIndex );
	}

	// The window is pumped from idCommonLocal::Frame instead - this function
	// runs on the render backend thread, which owns no windows. See
	// PCVR_FlatPumpWindow in win_pcvr.cpp.
	PCVR_ApplySwapInterval();		// context thread; see the note there
	SwapBuffers( pcvr_flatDC );
}

// ---------------------------------------------------------------------------
// PCVR: the desktop mirror. For streaming, because Virtual Desktop's own
// preview is awkward to capture.
//
// The same blit flatscreen mode already does, driven while VR is running: one
// eye out of the array texture, into the window that has existed all along as
// the GL context's home and simply was never shown.
//
// Off by default, and it stays off unless somebody asks. A mirror in a VR port
// is exactly the thing that can pin a 90 Hz session to the monitor's refresh -
// on CSVR that cost 15 ms a frame while the game itself rendered in 1.6, and
// the tell was a frame time locked to 16.66 ms with the compositor wait at
// zero. So: swap interval 0 where the driver will honour it, half resolution by
// default because a stream does not need the headset's pixel count, and the
// cost is timed and printed in the run report rather than left to be argued
// about. If it ever starts costing a frame, the log will say so first.
static int   pcvr_mirrorShown = 0;

// PCVR: what paces the mirror, and why it is a clock and not a fence.
//
// Presenting every frame pinned the whole session to the monitor: 90 fps with
// the mirror off, 60 with it on, measured in the headset.
//
// The first attempt at this was a GL fence taken after SwapBuffers, on the
// theory that it would signal when the display had absorbed the frame. It does
// not. glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE ) signals when the commands
// already issued - the blit - have finished on the GPU, which is microseconds,
// and it knows nothing about presentation. The run report said so in one number:
// 956 mirror frames, SKIPPED BY THE FENCE: 1. It never fired, so it never
// paced anything, and the session was still at 60.
//
// What actually blocks is the swap queue. A windowed GL present is absorbed at
// the desktop compositor's refresh whatever wglSwapIntervalEXT(0) returned;
// asking 90 times a second against a 60 Hz desktop keeps the queue full, and
// the next call touching the default framebuffer waits for a vblank. That is
// also why the cost appeared inside our own blit rather than at SwapBuffers.
//
// So: present less often than the desktop can absorb, and the queue always has
// room. The old comment here warned that a cap is the wrong answer, and it was
// half right - a cap AT the refresh leaves no slack and re-creates the stall.
// A cap well UNDER it is exactly the fix. pcvr_mirrorHz, default 30.
#define PCVR_MIRROR_DEFAULT_HZ 30

static int    pcvr_mirrorSkips = 0;
static double pcvr_mirrorNextDue = 0.0;		// QPC seconds

// Split timing, so the next run report says WHICH half costs the frame rather
// than leaving it to be argued about a second time.
static double pcvr_mirrorBlitSum = 0.0;
static double pcvr_mirrorSwapSum = 0.0;
static int    pcvr_mirrorTimed = 0;

// What the driver said about the swap interval. Recorded rather than logged,
// because ALOGE is printf on Windows and a GUI app has no stdout - every ALOGE
// in this layer has been going nowhere. These travel to the run report.
static int pcvr_swapAsked = -2;		// -2 no attempt, -1 no entry point
static int pcvr_swapReturn = 0;
static int pcvr_swapReadBack = -2;	// -2 not read, -1 no entry point

int PCVR_MirrorSkips( void ) {
	return pcvr_mirrorSkips;
}

void PCVR_MirrorStats( double* blitUs, double* swapUs, int* timed,
		int* asked, int* ret, int* readBack ) {
	*blitUs = pcvr_mirrorTimed ? ( pcvr_mirrorBlitSum / pcvr_mirrorTimed ) : 0.0;
	*swapUs = pcvr_mirrorTimed ? ( pcvr_mirrorSwapSum / pcvr_mirrorTimed ) : 0.0;
	*timed = pcvr_mirrorTimed;
	*asked = pcvr_swapAsked;
	*ret = pcvr_swapReturn;
	*readBack = pcvr_swapReadBack;
}

// Turn vsync off, from the thread that can. MUST be called with the GL context
// current, which means from a present path and not from the window-management
// half: wglGetProcAddress resolves against the current context and returns NULL
// when the calling thread has none, and opengl32.dll does not export
// wglSwapIntervalEXT, so the loader's fallback misses too.
//
// That is not hypothetical. Both earlier callers were on the main thread -
// PCVR_MirrorStart and PCVR_FlatWindowShow, which run from
// Doom3Quest_FrameSetup because ShowWindow from the backend deadlocks - so the
// request was silently dropped every time this port has ever run, while the
// comment beside it said "vsync off".
void PCVR_ApplySwapInterval( void ) {
	typedef BOOL (WINAPI *PFN_wglSwapIntervalEXT)( int );
	typedef int  (WINAPI *PFN_wglGetSwapIntervalEXT)( void );
	PFN_wglSwapIntervalEXT p_set;
	PFN_wglGetSwapIntervalEXT p_get;

	if ( pcvr_swapAsked != -2 ) {
		return;					// once is enough; it is per-context state
	}

	p_set = (PFN_wglSwapIntervalEXT)PCVR_GL_GetProc( "wglSwapIntervalEXT" );
	if ( !p_set ) {
		pcvr_swapAsked = -1;
		return;
	}

	// Asking is still not the same as being obeyed - a driver-level "force
	// vsync" overrides it, and under desktop composition a windowed present can
	// be paced by the compositor whatever this returns. So read it back, and
	// send both facts to the run report. Nothing could say this before: ALOGE
	// is printf on Windows and this is a GUI app with no stdout.
	p_get = (PFN_wglGetSwapIntervalEXT)PCVR_GL_GetProc( "wglGetSwapIntervalEXT" );

	pcvr_swapAsked = 0;
	pcvr_swapReturn = p_set( 0 ) ? 1 : 0;
	pcvr_swapReadBack = p_get ? p_get() : -1;
}

static double PCVR_Seconds( void ) {
	static LARGE_INTEGER freq = { 0 };
	LARGE_INTEGER now;

	if ( !freq.QuadPart ) {
		QueryPerformanceFrequency( &freq );
	}
	QueryPerformanceCounter( &now );
	return (double)now.QuadPart / (double)freq.QuadPart;
}

// May the mirror present this frame? Called from the backend, outside the
// timing brackets, so a skipped frame costs nothing and is not averaged in as
// a cheap one.
int PCVR_MirrorDue( int hz ) {
	double now, period;

	if ( hz <= 0 ) {
		// Automatic: follow the monitor.
		//
		// There is no rate that makes this smooth, and it is worth writing down
		// why so nobody chases it again. The headset runs at 90 and a 60 Hz
		// monitor cannot show 90 of anything. Present 60 and two frames in
		// three are kept, so motion advances 1-1-2 - 3:2 pulldown judder, the
		// thing that makes 24 fps film look wrong on a 60 Hz TV. Present 90 and
		// the panel samples them unevenly instead, with tearing, because the
		// swap interval is 0. Present 30 and the cadence is finally even, since
		// 30 divides both 90 and 60 - and it looks like 30 fps, which is the
		// complaint that started this.
		//
		// A decoupled present thread would not fix it either: the source is
		// still 90 and the display is still 60. Only matching the rates does -
		// a 90 or 120 Hz monitor, or a 60 Hz headset.
		//
		// So: the monitor's own refresh, which is the conventional choice and
		// the most frames it can actually show. Except when vsync is stuck on,
		// where the cap stops being cosmetic and starts being the thing that
		// keeps a full present queue from stalling the VR frame.
		if ( pcvr_swapReadBack != 0 ) {
			hz = PCVR_MIRROR_DEFAULT_HZ;
		} else {
			hz = pcvr_flatDC ? GetDeviceCaps( pcvr_flatDC, VREFRESH ) : 0;
			if ( hz < 5 ) {
				hz = 60;		// 0 or 1 means "default"; no useful answer
			}
		}
	}
	now = PCVR_Seconds();
	if ( now < pcvr_mirrorNextDue ) {
		pcvr_mirrorSkips++;
		return 0;
	}

	// Advance by whole periods rather than from now. Scheduling from now adds
	// however late this frame was to every interval - 30 Hz asked came out at
	// 22.5 measured on the desk, because "now" is already up to a frame past
	// due. Resync if it has fallen more than a period behind, so a level load
	// cannot produce a catch-up burst of back-to-back presents, which would
	// fill the swap queue and stall exactly the way presenting every frame did.
	period = 1.0 / (double)hz;
	pcvr_mirrorNextDue += period;
	if ( pcvr_mirrorNextDue < now ) {
		pcvr_mirrorNextDue = now + period;
	}
	return 1;
}

static GLuint pcvr_mirrorFBO = 0;

// Work out what part of the eye goes where in the window, so the picture keeps
// its shape. See pcvr_mirrorFit for what the modes mean.
//
// GL blits a source rect to a destination rect, so "crop" is a smaller source
// rect and "fit" is a smaller destination rect; neither costs anything extra.
static void PCVR_MirrorFitRects( int fit, float cropY, int sw, int sh,
		int dw, int dh, int* s, int* d, int* needClear ) {
	double srcAspect = (double)sw / (double)sh;		// mode 3 narrows this
	const double dstAspect = (double)dw / (double)dh;

	s[0] = 0;  s[1] = 0;  s[2] = sw;  s[3] = sh;
	d[0] = 0;  d[1] = 0;  d[2] = dw;  d[3] = dh;
	*needClear = 0;

	if ( fit == 0 || sw < 1 || sh < 1 || dw < 1 || dh < 1 ) {
		return;					// stretch, or nothing sensible to do
	}

	if ( fit == 3 ) {
		// The screen layer - menus, cinematics, loading, the console - draws the
		// game's 2D on a panel that spans the eye's full WIDTH in a 4:3 shape,
		// because idTech4 guis are a 640x480 space. The eye is taller than that,
		// so a third of its height is the panel's own black surround.
		//
		// Fitting the whole eye therefore spends a third of the monitor on black
		// and leaves the menu small in the middle, which was the reported symptom. Frame
		// the panel instead: take the 4:3 band out of the middle of the eye and
		// letterbox THAT. Measured from a capture at 1280x720 - the panel ran
		// the full 648 px width of the fitted eye and 485 of its 720 px height,
		// which is 4:3 to within a pixel.
		const double panelAspect = 4.0 / 3.0;
		int bandH = (int)( sw / panelAspect + 0.5 );

		if ( bandH > sh ) {
			bandH = sh;
		}
		s[1] = ( sh - bandH ) / 2;
		s[3] = s[1] + bandH;

		fit = 1;				// and letterbox what is left, below
		srcAspect = (double)( s[2] - s[0] ) / (double)( s[3] - s[1] );
	}

	if ( fit == 1 ) {
		// Shrink the destination until it is the source's shape. Whatever is
		// left over is black, so the back buffer has to be cleared: it holds
		// the frame before last and the bars would flicker with old picture.
		int w = dw, h = dh;

		if ( dstAspect > srcAspect ) {
			w = (int)( dh * srcAspect + 0.5 );
		} else {
			h = (int)( dw / srcAspect + 0.5 );
		}
		d[0] = ( dw - w ) / 2;
		d[1] = ( dh - h ) / 2;
		d[2] = d[0] + w;
		d[3] = d[1] + h;
		*needClear = 1;
		return;
	}

	// fit == 2: shrink the SOURCE until it is the window's shape.
	if ( dstAspect > srcAspect ) {
		// Window is wider than the eye - keep full width, take a band of rows.
		const int h = (int)( sw / dstAspect + 0.5 );
		int y0;

		if ( cropY < 0.0f ) { cropY = 0.0f; }
		if ( cropY > 1.0f ) { cropY = 1.0f; }
		y0 = (int)( ( sh - h ) * cropY + 0.5 );
		if ( y0 < 0 ) { y0 = 0; }
		if ( y0 + h > sh ) { y0 = sh - h; }

		// The blit's source origin is the bottom-left, and cropY is measured
		// from the TOP so it reads the way a person describes a picture.
		s[1] = sh - ( y0 + h );
		s[3] = s[1] + h;
	} else {
		const int w = (int)( sh * dstAspect + 0.5 );

		s[0] = ( sw - w ) / 2;
		s[2] = s[0] + w;
	}
}

void PCVR_MirrorPresent( ovrFramebuffer* fb, int eye, float scale, int fit, float cropY ) {
	static PFN_PCVR_glFramebufferTextureLayer p_layer = NULL;
	static PFN_PCVR_glBlitFramebuffer p_blit = NULL;
	int sw, sh, dw, dh;
	int src[4], dst[4], needClear;
	GLuint tex;
	RECT rc;
	double tBlit, tSwap;

	if ( !fb || !fb->ColorSwapChainImage || !pcvr_mirrorShown || !pcvr_flatDC ) {
		return;
	}
	if ( !p_layer ) {
		p_layer = (PFN_PCVR_glFramebufferTextureLayer)
				PCVR_GL_GetProc( "glFramebufferTextureLayer" );
	}
	if ( !p_blit ) {
		p_blit = (PFN_PCVR_glBlitFramebuffer)PCVR_GL_GetProc( "glBlitFramebuffer" );
	}
	if ( !p_layer || !p_blit ) {
		return;
	}

	// Here rather than in PCVR_MirrorStart: this is the context thread.
	PCVR_ApplySwapInterval();

	if ( !pcvr_mirrorFBO ) {
		glGenFramebuffers( 1, &pcvr_mirrorFBO );
	}

	tex = fb->ColorSwapChainImage[fb->TextureSwapChainIndex].image;
	sw = (int)fb->ColorSwapChain.Width;
	sh = (int)fb->ColorSwapChain.Height;

	// The window's client area, not the eye buffer: the mirror is whatever size
	// the streamer dragged it to, and the blit scales into it.
	dw = (int)( sw * scale );
	dh = (int)( sh * scale );
	if ( pcvr_flatHWND && GetClientRect( pcvr_flatHWND, &rc ) ) {
		if ( rc.right > 0 && rc.bottom > 0 ) {
			dw = rc.right;
			dh = rc.bottom;
		}
	}

	tBlit = PCVR_Seconds();

	glBindFramebuffer( GL_READ_FRAMEBUFFER, pcvr_mirrorFBO );
	p_layer( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, eye ? 1 : 0 );

	// The first touch of the default framebuffer, and therefore where a full
	// swap queue actually makes us wait - not SwapBuffers, which returns in
	// microseconds. Counted in the blit half for that reason.
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );

	// Same reasoning as the flatscreen blit: the engine writes linear values
	// into an sRGB texture with GL_FRAMEBUFFER_SRGB off, so the bytes are moved
	// rather than reinterpreted.
	glDisable( GL_FRAMEBUFFER_SRGB );

	PCVR_MirrorFitRects( fit, cropY, sw, sh, dw, dh, src, dst, &needClear );
	if ( needClear ) {
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	p_blit( src[0], src[1], src[2], src[3],
			dst[0], dst[1], dst[2], dst[3], GL_COLOR_BUFFER_BIT, GL_LINEAR );

	glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );

	tSwap = PCVR_Seconds();
	SwapBuffers( pcvr_flatDC );

	pcvr_mirrorBlitSum += ( tSwap - tBlit ) * 1000000.0;
	pcvr_mirrorSwapSum += ( PCVR_Seconds() - tSwap ) * 1000000.0;
	pcvr_mirrorTimed++;
}

// Show the window and ask the driver not to wait for the monitor. Called once,
// from the backend, the first time the mirror is wanted.
void PCVR_MirrorStart( ovrEgl* egl, int width, int height ) {
	if ( pcvr_mirrorShown || !egl || !egl->Window ) {
		return;
	}
	PCVR_FlatWindowShow( egl, width, height );
	SetWindowTextA( egl->Window, "PreyVR - mirror" );

	// The swap interval is NOT set here. This runs on the main thread - see
	// PCVR_ApplySwapInterval, which the present path calls instead.
	pcvr_mirrorShown = 1;
}

// Borderless fullscreen, and back. MAIN THREAD ONLY - see PCVR_MirrorStart.
//
// Borderless rather than exclusive: an exclusive-fullscreen GL window owns the
// display mode and would put the mirror back in the business of costing the
// headset frames, which is the thing this whole area exists to avoid.
void PCVR_MirrorSetFullscreen( int on ) {
	static int applied = 0;
	static int haveSaved = 0;
	static LONG savedStyle = 0;
	static WINDOWPLACEMENT savedPlace;

	if ( !pcvr_mirrorShown || !pcvr_flatHWND ) {
		return;
	}
	on = on ? 1 : 0;
	if ( on == applied ) {
		return;
	}

	if ( on ) {
		HMONITOR mon;
		MONITORINFO mi;

		savedStyle = GetWindowLongA( pcvr_flatHWND, GWL_STYLE );
		savedPlace.length = sizeof( savedPlace );
		haveSaved = GetWindowPlacement( pcvr_flatHWND, &savedPlace ) ? 1 : 0;

		// The monitor it is already on, so dragging it somewhere and then going
		// fullscreen does what the person meant.
		mon = MonitorFromWindow( pcvr_flatHWND, MONITOR_DEFAULTTONEAREST );
		mi.cbSize = sizeof( mi );
		if ( !mon || !GetMonitorInfoA( mon, &mi ) ) {
			return;
		}

		SetWindowLongA( pcvr_flatHWND, GWL_STYLE,
				( savedStyle & ~( WS_OVERLAPPEDWINDOW ) ) | WS_POPUP );
		SetWindowPos( pcvr_flatHWND, NULL,
				mi.rcMonitor.left, mi.rcMonitor.top,
				mi.rcMonitor.right - mi.rcMonitor.left,
				mi.rcMonitor.bottom - mi.rcMonitor.top,
				SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW );
		applied = 1;
		return;
	}

	SetWindowLongA( pcvr_flatHWND, GWL_STYLE, savedStyle ? savedStyle : WS_OVERLAPPEDWINDOW );
	if ( haveSaved ) {
		SetWindowPlacement( pcvr_flatHWND, &savedPlace );
	}
	SetWindowPos( pcvr_flatHWND, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW );
	applied = 0;
}

void PCVR_MirrorStop( void ) {
	if ( pcvr_mirrorShown && pcvr_flatHWND ) {
		ShowWindow( pcvr_flatHWND, SW_HIDE );
	}
	// Put the frame back before hiding it, or turning the mirror off and on
	// again returns a borderless window that thinks it is windowed.
	if ( pcvr_mirrorShown ) {
		PCVR_MirrorSetFullscreen( 0 );
	}
	pcvr_mirrorNextDue = 0.0;
	pcvr_mirrorShown = 0;
}

#endif

bool ovrFramebuffer_Create(
		XrSession session,
		ovrFramebuffer* frameBuffer,
		const bool useMultiview,
		const int width,
		const int height,
		int multisamples) {   // PCVR: not const - see the extension gate below

	frameBuffer->Width = width;
	frameBuffer->Height = height;
	frameBuffer->Multisamples = multisamples;
	frameBuffer->UseMultiview = useMultiview;

	PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC glRenderbufferStorageMultisampleEXT =
			(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)eglGetProcAddress(
					"glRenderbufferStorageMultisampleEXT");
	PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT =
			(PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)eglGetProcAddress(
					"glFramebufferTexture2DMultisampleEXT");

	PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR =
			(PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)eglGetProcAddress(
					"glFramebufferTextureMultiviewOVR");
	PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC glFramebufferTextureMultisampleMultiviewOVR =
			(PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)eglGetProcAddress(
					"glFramebufferTextureMultisampleMultiviewOVR");

#if defined( _WIN32 )
	// PCVR: see PCVR_HasGLExtension above - a non-NULL pointer here means
	// nothing on this platform, so ask whether the extension actually exists.
	if ( !PCVR_HasGLExtension( "GL_OVR_multiview_multisampled_render_to_texture" ) ) {
		if ( multisamples > 1 ) {
			ALOGE( "PCVR: no GL_OVR_multiview_multisampled_render_to_texture; "
				   "dropping MSAA %d to 1\n", multisamples );
		}

		// The sample count has to come down with it. swapChainCreateInfo.sampleCount
		// below is set from this same value, so leaving it at 4 while attaching
		// through the non-multisampled glFramebufferTextureMultiviewOVR gives a
		// 4-sample array texture on a single-sample attachment point: the
		// framebuffer comes out incomplete and the headset shows black.
		multisamples = 1;
		frameBuffer->Multisamples = multisamples;
		glFramebufferTextureMultisampleMultiviewOVR = NULL;
	}

	if ( !PCVR_HasGLExtension( "GL_EXT_multisampled_render_to_texture" ) ) {
		glFramebufferTexture2DMultisampleEXT = NULL;
		glRenderbufferStorageMultisampleEXT = NULL;
	}
#endif

#if defined( _WIN32 )
	// PCVR: flat mode has no session and therefore no swapchain. Everything the
	// loop below needs is a GL array texture of the right shape, so make one.
	// The Milestone 1 probe already proved this driver builds a COMPLETE
	// multiview FBO over a locally-created 2-layer array texture.
	if ( PCVR_Flatscreen() ) {
		frameBuffer->ColorSwapChain.Width = width;
		frameBuffer->ColorSwapChain.Height = height;
		frameBuffer->ColorSwapChain.Handle = XR_NULL_HANDLE;
		frameBuffer->TextureSwapChainLength = 1;
		frameBuffer->TextureSwapChainIndex = 0;

		frameBuffer->ColorSwapChainImage =
				(XrSwapchainImageOpenGLKHR*)malloc( sizeof( XrSwapchainImageOpenGLKHR ) );
		frameBuffer->ColorSwapChainImage[0].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
		frameBuffer->ColorSwapChainImage[0].next = NULL;

		if ( !useMultiview ) {
			// Never happens: this port is multiview everywhere, and the
			// single-view path would need glTexStorage2D, which the GL loader
			// does not carry. Fail loudly rather than link a dead branch.
			ALOGE( "PCVR: flatscreen requires multiview\n" );
			return false;
		}

		GLuint colour = 0;
		GL( glGenTextures( 1, &colour ) );
		GL( glBindTexture( GL_TEXTURE_2D_ARRAY, colour ) );
		// Same format the OpenXR path asks for, so the engine's own
		// GL_FRAMEBUFFER_SRGB handling behaves identically either way.
		GL( glTexStorage3D( GL_TEXTURE_2D_ARRAY, 1, GL_SRGB8_ALPHA8, width, height, 2 ) );
		GL( glBindTexture( GL_TEXTURE_2D_ARRAY, 0 ) );
		frameBuffer->ColorSwapChainImage[0].image = colour;

		ALOGE( "PCVR: flatscreen colour texture %u, %dx%d x%d layers\n",
			   colour, width, height, useMultiview ? 2 : 1 );
	} else {
#endif

	XrSwapchainCreateInfo swapChainCreateInfo;
	memset(&swapChainCreateInfo, 0, sizeof(swapChainCreateInfo));
	swapChainCreateInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	swapChainCreateInfo.sampleCount = multisamples;
	swapChainCreateInfo.width = width;
	swapChainCreateInfo.height = height;
	swapChainCreateInfo.faceCount = 1;
	swapChainCreateInfo.arraySize = useMultiview ? 2 : 1;
	swapChainCreateInfo.mipCount = 1;

	frameBuffer->ColorSwapChain.Width = swapChainCreateInfo.width;
	frameBuffer->ColorSwapChain.Height = swapChainCreateInfo.height;

	// PCVR: ask the runtime what it would rather have, and write it down.
	//
	// The format below has always been hard-coded and xrEnumerateSwapchainFormats
	// has never been called, so nobody has ever known whether it is the format
	// VirtualDesktopXR wants or one it has to convert. The spec says the list
	// comes back in the runtime's own order of preference, so index 0 is its
	// first choice.
	//
	// This matters because the run report says xrEndFrame costs a mean of 1.28 ms and
	// runs over 2 ms on 20% of frames, against 1.1% on SteamVR with the same
	// code. This is an OpenGL client on a Direct3D runtime; a format the runtime
	// has to convert is one of the few things that could cost that much, and it
	// is the cheapest one to find out about.
	//
	// Nothing is changed yet on purpose. Read the list first.
	{
		uint32_t formatCount = 0;
		char line[160];

		if ( XR_SUCCEEDED( xrEnumerateSwapchainFormats( session, 0, &formatCount, NULL ) ) && formatCount > 0 ) {
			int64_t *formats = (int64_t *)malloc( formatCount * sizeof( int64_t ) );

			if ( formats ) {
				uint32_t got = 0;

				if ( XR_SUCCEEDED( xrEnumerateSwapchainFormats( session, formatCount, &got, formats ) ) ) {
					int mine = -1;

					for ( uint32_t i = 0; i < got; i++ ) {
						const char *name = "?";

						switch ( (int)formats[i] ) {
							case 0x8058: name = "GL_RGBA8"; break;
							case 0x8C43: name = "GL_SRGB8_ALPHA8"; break;
							case 0x881A: name = "GL_RGBA16F"; break;
							case 0x8059: name = "GL_RGB10_A2"; break;
							case 0x8C41: name = "GL_SRGB8"; break;
							case 0x8051: name = "GL_RGB8"; break;
							case 0x881B: name = "GL_RGB16F"; break;
							case 0x8C3A: name = "GL_R11F_G11F_B10F"; break;
							case 0x81A5: name = "GL_DEPTH_COMPONENT16"; break;
							case 0x81A6: name = "GL_DEPTH_COMPONENT24"; break;
							case 0x8CAC: name = "GL_DEPTH_COMPONENT32F"; break;
							case 0x88F0: name = "GL_DEPTH24_STENCIL8"; break;
							default: break;
						}
						if ( (int)formats[i] == GL_SRGB8_ALPHA8 ) {
							mine = (int)i;
						}
						snprintf( line, sizeof( line ), "PCVR swapchain format %u: 0x%04X %s%s",
								  i, (unsigned)formats[i], name,
								  i == 0 ? "   <- the runtime's first choice" : "" );
						PCVR_Log( line );
					}
					snprintf( line, sizeof( line ), "PCVR: we ask for GL_SRGB8_ALPHA8, which is at index %d of %u",
							  mine, got );
					PCVR_Log( line );
				}
				free( formats );
			}
		}
	}
	{
		char eye[128];
		snprintf( eye, sizeof( eye ), "PCVR eye buffer: %dx%d, %d layer(s), %d sample(s)",
				  (int)swapChainCreateInfo.width, (int)swapChainCreateInfo.height,
				  (int)swapChainCreateInfo.arraySize, (int)swapChainCreateInfo.sampleCount );
		PCVR_Log( eye );
	}

	// Create the color swapchain.
	swapChainCreateInfo.format = GL_SRGB8_ALPHA8;
	swapChainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
	OXR(xrCreateSwapchain(session, &swapChainCreateInfo, &frameBuffer->ColorSwapChain.Handle));

	// Get the number of swapchain images.
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->ColorSwapChain.Handle, 0, &frameBuffer->TextureSwapChainLength, NULL));

	// Allocate the swapchain images array.
#ifdef ANDROID
	frameBuffer->ColorSwapChainImage = (XrSwapchainImageOpenGLESKHR*)malloc(
			frameBuffer->TextureSwapChainLength * sizeof(XrSwapchainImageOpenGLESKHR));
#else
	// PCVR: XR_KHR_opengl_enable hands back the non-ES struct. Same layout.
	frameBuffer->ColorSwapChainImage = (XrSwapchainImageOpenGLKHR*)malloc(
			frameBuffer->TextureSwapChainLength * sizeof(XrSwapchainImageOpenGLKHR));
#endif

	// Populate the swapchain image array.
	for (uint32_t i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
#ifdef ANDROID
		frameBuffer->ColorSwapChainImage[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
#else
		frameBuffer->ColorSwapChainImage[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
#endif
		frameBuffer->ColorSwapChainImage[i].next = NULL;
	}
	OXR(xrEnumerateSwapchainImages(
			frameBuffer->ColorSwapChain.Handle,
			frameBuffer->TextureSwapChainLength,
			&frameBuffer->TextureSwapChainLength,
			(XrSwapchainImageBaseHeader*)frameBuffer->ColorSwapChainImage));

#if defined( _WIN32 )
	}   // PCVR: end of the swapchain path skipped in flat mode
#endif

	frameBuffer->DepthBuffers =
			(GLuint*)malloc(frameBuffer->TextureSwapChainLength * sizeof(GLuint));
	frameBuffer->FrameBuffers =
			(GLuint*)malloc(frameBuffer->TextureSwapChainLength * sizeof(GLuint));

	ALOGV("		frameBuffer->UseMultiview = %d", frameBuffer->UseMultiview);

	for (int i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
		// Create the color buffer texture.
		const GLuint colorTexture = frameBuffer->ColorSwapChainImage[i].image;
		GLenum colorTextureTarget = frameBuffer->UseMultiview ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
		GL(glBindTexture(colorTextureTarget, colorTexture));
		GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
		GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
		GLfloat borderColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
		GL(glTexParameterfv(colorTextureTarget, GL_TEXTURE_BORDER_COLOR, borderColor));
		GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		GL(glBindTexture(colorTextureTarget, 0));

		if (frameBuffer->UseMultiview) {
			// Create the depth buffer texture.
			GL(glGenTextures(1, &frameBuffer->DepthBuffers[i]));
			GL(glBindTexture(GL_TEXTURE_2D_ARRAY, frameBuffer->DepthBuffers[i]));
			GL(glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH24_STENCIL8, width, height, 2));
			GL(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));

			// Create the frame buffer.
			GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
			GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
			if (multisamples > 1 && (glFramebufferTextureMultisampleMultiviewOVR != NULL)) {
				GL(glFramebufferTextureMultisampleMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_DEPTH_ATTACHMENT,
						frameBuffer->DepthBuffers[i],
						0 /* level */,
						multisamples /* samples */,
						0 /* baseViewIndex */,
						2 /* numViews */));
				GL(glFramebufferTextureMultisampleMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						frameBuffer->DepthBuffers[i],
						0 /* level */,
						multisamples /* samples */,
						0 /* baseViewIndex */,
						2 /* numViews */));
				GL(glFramebufferTextureMultisampleMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0,
						colorTexture,
						0 /* level */,
						multisamples /* samples */,
						0 /* baseViewIndex */,
						2 /* numViews */));
			} else {
				GL(glFramebufferTextureMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_DEPTH_ATTACHMENT,
						frameBuffer->DepthBuffers[i],
						0 /* level */,
						0 /* baseViewIndex */,
						2 /* numViews */));
				GL(glFramebufferTextureMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						frameBuffer->DepthBuffers[i],
						0 /* level */,
						0 /* baseViewIndex */,
						2 /* numViews */));
				GL(glFramebufferTextureMultiviewOVR(
						GL_DRAW_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0,
						colorTexture,
						0 /* level */,
						0 /* baseViewIndex */,
						2 /* numViews */));
			}

			GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
			GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
			if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
				ALOGE("Incomplete frame buffer object: %d", renderFramebufferStatus);
				return false;
			}
		} else {
			if (multisamples > 1 && glRenderbufferStorageMultisampleEXT != NULL &&
				glFramebufferTexture2DMultisampleEXT != NULL) {
				// Create multisampled depth buffer.
				GL(glGenRenderbuffers(1, &frameBuffer->DepthBuffers[i]));
				GL(glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer->DepthBuffers[i]));
				GL(glRenderbufferStorageMultisampleEXT(
						GL_RENDERBUFFER, multisamples, GL_DEPTH24_STENCIL8, width, height));
				GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

				// Create the frame buffer.
				// NOTE: glFramebufferTexture2DMultisampleEXT only works with GL_FRAMEBUFFER.
				GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
				GL(glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
				GL(glFramebufferTexture2DMultisampleEXT(
						GL_FRAMEBUFFER,
						GL_COLOR_ATTACHMENT0,
						GL_TEXTURE_2D,
						colorTexture,
						0,
						multisamples));
				GL(glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_DEPTH_ATTACHMENT,
						GL_RENDERBUFFER,
						frameBuffer->DepthBuffers[i]));
				GL(glFramebufferRenderbuffer(
						GL_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						frameBuffer->DepthBuffers[i]));
				GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER));
				GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
				if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
					ALOGE("Incomplete frame buffer object: %d", renderFramebufferStatus);
					return false;
				}
			} else {
				// Create depth buffer.
				GL(glGenRenderbuffers(1, &frameBuffer->DepthBuffers[i]));
				GL(glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer->DepthBuffers[i]));
				GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height));
				GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

				// Create the frame buffer.
				GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
				GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
				GL(glFramebufferRenderbuffer(
						GL_DRAW_FRAMEBUFFER,
						GL_DEPTH_ATTACHMENT,
						GL_RENDERBUFFER,
						frameBuffer->DepthBuffers[i]));
				GL(glFramebufferRenderbuffer(
						GL_DRAW_FRAMEBUFFER,
						GL_STENCIL_ATTACHMENT,
						GL_RENDERBUFFER,
						frameBuffer->DepthBuffers[i]));
				GL(glFramebufferTexture2D(
						GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0));
				GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
				GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
				if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
					ALOGE("Incomplete frame buffer object: %d", renderFramebufferStatus);
					return false;
				}
			}
		}
	}

	return true;
}

void ovrFramebuffer_Destroy(ovrFramebuffer* frameBuffer) {
	GL(glDeleteFramebuffers(frameBuffer->TextureSwapChainLength, frameBuffer->FrameBuffers));
	OXR(xrDestroySwapchain(frameBuffer->ColorSwapChain.Handle));
	free(frameBuffer->ColorSwapChainImage);
	if (frameBuffer->UseMultiview) {
		GL(glDeleteTextures(frameBuffer->TextureSwapChainLength, frameBuffer->DepthBuffers));
	} else {
		GL(glDeleteRenderbuffers(frameBuffer->TextureSwapChainLength, frameBuffer->DepthBuffers));
	}
	free(frameBuffer->DepthBuffers);
	free(frameBuffer->FrameBuffers);

	ovrFramebuffer_Clear(frameBuffer);
}

void ovrFramebuffer_SetCurrent(ovrFramebuffer* frameBuffer) {
	GL(glBindFramebuffer(
			GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[frameBuffer->TextureSwapChainIndex]));

	//This is a bit of a hack, but we need to do this to correct for the fact that the engine uses linear RGB colorspace
	//but openxr uses SRGB (or something, must admit I don't really understand, but adding this works to make it look good again)
	glDisable( GL_FRAMEBUFFER_SRGB );
}

void ovrFramebuffer_SetNone() {
	GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
}

void ovrFramebuffer_Resolve(ovrFramebuffer* frameBuffer) {
	// Discard the depth buffer, so the tiler won't need to write it back out to memory.
	const GLenum depthAttachment[1] = {GL_DEPTH_ATTACHMENT};
	glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, depthAttachment);
}

void ovrFramebuffer_Acquire(ovrFramebuffer* frameBuffer) {
	// Acquire the swapchain image
	XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO, NULL};
	PCVR_Stage("xrAcquireSwapchainImage");
	PCVR_XrIn();
	OXR(xrAcquireSwapchainImage(
			frameBuffer->ColorSwapChain.Handle, &acquireInfo, &frameBuffer->TextureSwapChainIndex));
	PCVR_XrOut(1);

	XrSwapchainImageWaitInfo waitInfo;
	waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	waitInfo.next = NULL;
	// XR_INFINITE_DURATION: if the runtime never hands this image back there is
	// no timeout to save us, and this thread is gone for good. Named so the
	// freeze report can say so.
	waitInfo.timeout = XR_INFINITE_DURATION;
	PCVR_Stage("xrWaitSwapchainImage - no timeout, waiting on the runtime");
	PCVR_XrIn();
	OXR(xrWaitSwapchainImage(frameBuffer->ColorSwapChain.Handle, &waitInfo));
	PCVR_XrOut(2);
	PCVR_Stage("swapchain image acquired");
}

void ovrFramebuffer_Release(ovrFramebuffer* frameBuffer) {
	XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO, NULL};
	PCVR_Stage("xrReleaseSwapchainImage");
	PCVR_XrIn();
	OXR(xrReleaseSwapchainImage(frameBuffer->ColorSwapChain.Handle, &releaseInfo));
	PCVR_XrOut(3);
}

/*
================================================================================

ovrRenderer

================================================================================
*/

void ovrRenderer_Clear(ovrRenderer* renderer) {
	ovrFramebuffer_Clear(&renderer->FrameBuffer);
}

void ovrRenderer_Create(
		XrSession session,
		ovrRenderer* renderer,
		bool useMultiview,
		int suggestedEyeTextureWidth,
		int suggestedEyeTextureHeight,
		int multisamples) {
	// Create the frame buffers.
	ovrFramebuffer_Create(
			session,
			&renderer->FrameBuffer,
			useMultiview,
			suggestedEyeTextureWidth,
			suggestedEyeTextureHeight,
			multisamples);
}

void ovrRenderer_Destroy(ovrRenderer* renderer) {
	ovrFramebuffer_Destroy(&renderer->FrameBuffer);
}

void ovrRenderer_MouseCursor(ovrRenderer* renderer, int x, int y, int sx, int sy) {
#if XR_USE_GRAPHICS_API_OPENGL_ES || XR_USE_GRAPHICS_API_OPENGL
	GL(glEnable(GL_SCISSOR_TEST));
	GL(glScissor(x, y, sx, sy));
	GL(glViewport(x, y, sx, sy));
	GL(glClearColor(1.0f, 1.0f, 1.0f, 1.0f));
	GL(glClear(GL_COLOR_BUFFER_BIT));
	GL(glDisable(GL_SCISSOR_TEST));
#endif
}

#ifdef ANDROID
void ovrRenderer_SetFoveation(XrInstance* instance, XrSession* session, ovrRenderer* renderer, XrFoveationLevelFB level, float verticalOffset, XrFoveationDynamicFB dynamic) {
	PFN_xrCreateFoveationProfileFB pfnCreateFoveationProfileFB;
	OXR(xrGetInstanceProcAddr(*instance, "xrCreateFoveationProfileFB", (PFN_xrVoidFunction*)(&pfnCreateFoveationProfileFB)));

	PFN_xrDestroyFoveationProfileFB pfnDestroyFoveationProfileFB;
	OXR(xrGetInstanceProcAddr(*instance, "xrDestroyFoveationProfileFB", (PFN_xrVoidFunction*)(&pfnDestroyFoveationProfileFB)));

	PFN_xrUpdateSwapchainFB pfnUpdateSwapchainFB;
	OXR(xrGetInstanceProcAddr(*instance, "xrUpdateSwapchainFB", (PFN_xrVoidFunction*)(&pfnUpdateSwapchainFB)));

	XrFoveationLevelProfileCreateInfoFB levelProfileCreateInfo;
	memset(&levelProfileCreateInfo, 0, sizeof(levelProfileCreateInfo));
	levelProfileCreateInfo.type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB;
	levelProfileCreateInfo.level = level;
	levelProfileCreateInfo.verticalOffset = verticalOffset;
	levelProfileCreateInfo.dynamic = dynamic;

	XrFoveationProfileCreateInfoFB profileCreateInfo;
	memset(&profileCreateInfo, 0, sizeof(profileCreateInfo));
	profileCreateInfo.type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB;
	profileCreateInfo.next = &levelProfileCreateInfo;

	XrFoveationProfileFB foveationProfile;

	pfnCreateFoveationProfileFB(*session, &profileCreateInfo, &foveationProfile);

	XrSwapchainStateFoveationFB foveationUpdateState;
	memset(&foveationUpdateState, 0, sizeof(foveationUpdateState));
	foveationUpdateState.type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB;
	foveationUpdateState.profile = foveationProfile;

	pfnUpdateSwapchainFB(renderer->FrameBuffer.ColorSwapChain.Handle, (XrSwapchainStateBaseHeaderFB*)(&foveationUpdateState));

	pfnDestroyFoveationProfileFB(foveationProfile);
}
#endif

/*
================================================================================

ovrApp

================================================================================
*/

void ovrApp_Clear(ovrApp* app) {
	app->Focused = false;
	app->Instance = XR_NULL_HANDLE;
	app->Session = XR_NULL_HANDLE;
	memset(&app->ViewportConfig, 0, sizeof(XrViewConfigurationProperties));
	memset(&app->ViewConfigurationView, 0, ovrMaxNumEyes * sizeof(XrViewConfigurationView));
	app->SystemId = XR_NULL_SYSTEM_ID;
	app->HeadSpace = XR_NULL_HANDLE;
	app->StageSpace = XR_NULL_HANDLE;
	app->FakeStageSpace = XR_NULL_HANDLE;
	app->CurrentSpace = XR_NULL_HANDLE;
	app->SessionActive = false;
	app->SwapInterval = 1;
	app->MainThreadTid = 0;
	app->RenderThreadTid = 0;

	ovrRenderer_Clear(&app->Renderer);
}

void ovrApp_Destroy(ovrApp* app) {
	ovrApp_Clear(app);
}

void ovrApp_HandleSessionStateChanges(ovrApp* app, XrSessionState state) {
	if (state == XR_SESSION_STATE_READY) {
		XrSessionBeginInfo sessionBeginInfo;
		memset(&sessionBeginInfo, 0, sizeof(sessionBeginInfo));
		sessionBeginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
		sessionBeginInfo.next = NULL;
		sessionBeginInfo.primaryViewConfigurationType = app->ViewportConfig.viewConfigurationType;

		XrResult result;
		OXR(result = xrBeginSession(app->Session, &sessionBeginInfo));
		app->SessionActive = (result == XR_SUCCESS);
	} else if (state == XR_SESSION_STATE_STOPPING) {
		OXR(xrEndSession(app->Session));
		app->SessionActive = false;
	}
}

int ovrApp_HandleXrEvents(ovrApp* app) {
	XrEventDataBuffer eventDataBuffer = {0};
	int recenter = 0;

	// Poll for events
	for (;;) {
		XrEventDataBaseHeader* baseEventHeader = (XrEventDataBaseHeader*)(&eventDataBuffer);
		baseEventHeader->type = XR_TYPE_EVENT_DATA_BUFFER;
		baseEventHeader->next = NULL;
		XrResult r;
		OXR(r = xrPollEvent(app->Instance, &eventDataBuffer));
		if (r != XR_SUCCESS) {
			break;
		}

		switch (baseEventHeader->type) {
			case XR_TYPE_EVENT_DATA_EVENTS_LOST:
				ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_EVENTS_LOST event");
				break;
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				const XrEventDataInstanceLossPending* instance_loss_pending_event =
						(XrEventDataInstanceLossPending*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING event: time %f",
						FromXrTime(instance_loss_pending_event->lossTime));
			} break;
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
				ALOGV("xrPollEvent: received XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED event");
				break;
			case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
				const XrEventDataPerfSettingsEXT* perf_settings_event =
						(XrEventDataPerfSettingsEXT*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT event: type %d subdomain %d : level %d -> level %d",
						perf_settings_event->type,
						perf_settings_event->subDomain,
						perf_settings_event->fromLevel,
						perf_settings_event->toLevel);
			} break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				XrEventDataReferenceSpaceChangePending* ref_space_change_event =
						(XrEventDataReferenceSpaceChangePending*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING event: changed space: %d for session %p at time %f",
						ref_space_change_event->referenceSpaceType,
						(void*)ref_space_change_event->session,
						FromXrTime(ref_space_change_event->changeTime));
				recenter = 1;
			} break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const XrEventDataSessionStateChanged* session_state_changed_event =
						(XrEventDataSessionStateChanged*)(baseEventHeader);
				ALOGV(
						"xrPollEvent: received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: %d for session %p at time %f",
						session_state_changed_event->state,
						(void*)session_state_changed_event->session,
						FromXrTime(session_state_changed_event->time));

				switch (session_state_changed_event->state) {
					case XR_SESSION_STATE_FOCUSED:
						app->Focused = true;
						break;
					case XR_SESSION_STATE_VISIBLE:
						app->Focused = false;
						break;
					case XR_SESSION_STATE_READY:
					case XR_SESSION_STATE_STOPPING:
						ovrApp_HandleSessionStateChanges(app, session_state_changed_event->state);
						break;
					default:
						break;
				}
			} break;
			default:
				ALOGV("xrPollEvent: Unknown event");
				break;
		}
	}
	return recenter;
}
