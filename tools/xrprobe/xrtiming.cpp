// Is it the game, or is it the machine?
//
// This opens an OpenXR session, creates a swapchain the size the runtime asks
// for, and submits completely blank frames at the display rate for as long as
// you let it. It draws nothing. There is no engine, no game, no Prey - so every
// millisecond it reports belongs to the runtime, the driver, the link or
// Windows, and none of it belongs to an application.
//
// Written because PreyVR's own logs kept saying the same thing across a week of
// sessions on the same machine: the game hands its frame over about 1 ms after
// the compositor releases it, leaving roughly 10 ms of an 11.1 ms period
// unused - and then xrEndFrame takes longer than 2 ms on about a sixth of
// frames. That is not a number an application can do anything about, and it is
// not a number the application can be blamed for either. This measures it with
// the application removed.
//
// Read the numbers like this:
//
//   submit interval   should be one display period, over and over. A spread
//                     means frames are not arriving evenly.
//   xrEndFrame        the runtime's own cost. On SteamVR's null driver this is
//                     under a millisecond; if it is several here, with nothing
//                     being rendered, the cost is downstream of any game.
//   slack             what is left of the period after the app is done. With a
//                     blank frame this should be nearly the whole period; if it
//                     is not, the runtime is not releasing frames on time.
//
// Usage:
//   xrtiming.exe [seconds]        default 30
//
// Change one thing at a time between runs - driver, Virtual Desktop version,
// bitrate, codec, network - and compare. That is the whole point of it.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr.h>            // PreyVR bundles these flat, not under openxr/
#include <openxr_platform.h>

#include <GL/gl.h>
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

// Everything printed also goes to timing-report.txt beside the exe.
//
// Run by double-clicking the exe, the console printed the report and closed with it,
// and the run was lost. A diagnostic tool that only reports to a window you
// cannot keep is not much of a diagnostic tool.
static FILE *g_out;

static void Say( const char *fmt, ... ) {
    va_list a;
    va_start( a, fmt );
    vprintf( fmt, a );
    va_end( a );
    if ( g_out ) {
        va_start( a, fmt );
        vfprintf( g_out, fmt, a );
        va_end( a );
    }
}

// ------------------------------------------------------------------ timing
static LARGE_INTEGER qpcFreq;

static double NowMs( void ) {
    LARGE_INTEGER t;
    QueryPerformanceCounter( &t );
    return ( t.QuadPart * 1000.0 ) / (double)qpcFreq.QuadPart;
}

// A histogram in tenths of a millisecond, which is fine enough to see a
// compositor cadence and coarse enough to read on one screen.
#define BUCKETS 600
typedef struct {
    int    hist[ BUCKETS ];
    int    over;
    int    count;
    double sum, worst;
    const char *name;
} stat_t;

static void Add( stat_t *s, double ms ) {
    int b = (int)( ms * 10.0 );
    if ( b < 0 ) b = 0;
    if ( b >= BUCKETS ) s->over++; else s->hist[ b ]++;
    s->count++;
    s->sum += ms;
    if ( ms > s->worst ) s->worst = ms;
}

static double Pct( const stat_t *s, int pct ) {
    int total = 0, run = 0, i;
    for ( i = 0; i < BUCKETS; i++ ) total += s->hist[ i ];
    if ( total < 1 ) return 0.0;
    for ( i = 0; i < BUCKETS; i++ ) {
        run += s->hist[ i ];
        if ( run * 100 >= total * pct ) return i / 10.0;
    }
    return ( BUCKETS - 1 ) / 10.0;
}

static void Report( const stat_t *s, double periodMs ) {
    int i, shown = 0;

    if ( s->count < 1 ) { Say( "  %-22s no samples\n", s->name ); return; }
    Say( "  %-22s p50 %6.2f   p95 %6.2f   p99 %6.2f   worst %7.2f   mean %6.2f ms\n",
            s->name, Pct( s, 50 ), Pct( s, 95 ), Pct( s, 99 ), s->worst,
            s->sum / (double)s->count );
    if ( periodMs > 0 ) {
        int late = 0;
        for ( i = (int)( periodMs * 10.0 ) + 1; i < BUCKETS; i++ ) late += s->hist[ i ];
        late += s->over;
        Say( "  %-22s over one display period (%.2f ms): %d of %d (%.2f%%)\n",
                "", periodMs, late, s->count, ( 100.0 * late ) / (double)s->count );
    }
    Say( "  %-22s ", "" );
    for ( i = 0; i < BUCKETS && shown < 14; i++ ) {
        if ( s->hist[ i ] > 0 ) { Say( "%.1f:%d  ", i / 10.0, s->hist[ i ] ); shown++; }
    }
    if ( s->over ) Say( "over:%d", s->over );
    Say( "\n\n" );
}

// Every stall, with the second it happened and what we were inside.
//
// "worst 101 ms" says one thing happened. It does not say whether it happens
// on a timer or at random, and that is the whole difference between a
// scheduled task and contention. Evenly spaced means something is waking up;
// scattered means something is competing.
#define MAX_SPIKES 256
typedef struct { double at, ms; const char *where; SYSTEMTIME wall; } spike_t;
static spike_t g_spikes[ MAX_SPIKES ];
static int     g_nspikes;

static void NoteSpike( double atSec, double ms, const char *where ) {
    if ( g_nspikes < MAX_SPIKES ) {
        g_spikes[ g_nspikes ].at = atSec;
        g_spikes[ g_nspikes ].ms = ms;
        g_spikes[ g_nspikes ].where = where;
        GetLocalTime( &g_spikes[ g_nspikes ].wall );
        g_nspikes++;
    }
}

// ---------------------------------------------------------------- the load
//
// A blank frame stresses nothing. The link and the runtime were clean for 30
// seconds with one, which told us the wireless path is fine and told us nothing
// about what happens when a game is actually running.
//
// So this draws: random triangles, new ones every frame, into both eye layers.
// It is deliberately hostile to a video encoder - high entropy, no temporal
// coherence, nothing to predict from the previous frame - and it costs real
// fill rate at whatever the runtime's eye resolution is. That is the half of
// the workload the probe was missing, without a game anywhere near it.
//
// Still no engine and no Prey: if this stalls, no application could have
// avoided it either.
typedef void (APIENTRY *PFN_glGenFramebuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glFramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint);
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum);

static PFN_glGenFramebuffers         pGenFramebuffers;
static PFN_glBindFramebuffer         pBindFramebuffer;
static PFN_glFramebufferTextureLayer pFramebufferTextureLayer;
static PFN_glCheckFramebufferStatus  pCheckFramebufferStatus;

#define GL_FRAMEBUFFER_        0x8D40
#define GL_COLOR_ATTACHMENT0_  0x8CE0
#define GL_FRAMEBUFFER_COMPLETE_ 0x8CD5

static GLuint g_fbo;
static int    g_tris;          // 0 = blank, as before
static int    g_fboBad;        // framebuffer never became complete
static int    g_glErr;         // the draw was rejected

// "drawing the load  p50 0.20 ms" is CPU time to ISSUE the commands, not GPU
// time to run them - the GPU work lands later and is absorbed elsewhere. So a
// small number here does NOT mean nothing was drawn. These two counters are
// what actually distinguishes "cheap" from "silently did nothing", and without
// them I would have been guessing at which.

static void *GLProc( const char *n ) {
    void *p = (void *)wglGetProcAddress( n );
    if ( !p ) {
        HMODULE m = GetModuleHandleA( "opengl32.dll" );
        if ( m ) p = (void *)GetProcAddress( m, n );
    }
    return p;
}

static bool LoadGL( void ) {
    pGenFramebuffers         = (PFN_glGenFramebuffers)GLProc( "glGenFramebuffers" );
    pBindFramebuffer         = (PFN_glBindFramebuffer)GLProc( "glBindFramebuffer" );
    pFramebufferTextureLayer = (PFN_glFramebufferTextureLayer)GLProc( "glFramebufferTextureLayer" );
    pCheckFramebufferStatus  = (PFN_glCheckFramebufferStatus)GLProc( "glCheckFramebufferStatus" );
    return pGenFramebuffers && pBindFramebuffer && pFramebufferTextureLayer
        && pCheckFramebufferStatus;
}

// A cheap deterministic generator, so two runs draw the same thing and the
// only thing that differs between them is the machine.
static unsigned g_seed = 1;
static float Rnd( void ) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (float)( ( g_seed >> 8 ) & 0xFFFF ) / 65535.0f;
}

static void DrawLoad( GLuint tex, int w, int h, int frame ) {
    if ( g_tris <= 0 || !g_fbo ) return;

    g_seed = (unsigned)frame * 2654435761u + 1u;

    for ( int layer = 0; layer < 2; layer++ ) {
        pBindFramebuffer( GL_FRAMEBUFFER_, g_fbo );
        pFramebufferTextureLayer( GL_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0_, tex, 0, layer );
        if ( pCheckFramebufferStatus( GL_FRAMEBUFFER_ ) != GL_FRAMEBUFFER_COMPLETE_ ) {
            g_fboBad++;
            pBindFramebuffer( GL_FRAMEBUFFER_, 0 );
            return;
        }
        glViewport( 0, 0, w, h );
        glClearColor( Rnd(), Rnd(), Rnd(), 1.0f );
        glClear( GL_COLOR_BUFFER_BIT );

        glBegin( GL_TRIANGLES );
        for ( int i = 0; i < g_tris; i++ ) {
            float x = Rnd() * 2.0f - 1.0f, y = Rnd() * 2.0f - 1.0f;
            glColor3f( Rnd(), Rnd(), Rnd() );
            glVertex2f( x, y );
            glColor3f( Rnd(), Rnd(), Rnd() );
            glVertex2f( x + 0.25f, y );
            glColor3f( Rnd(), Rnd(), Rnd() );
            glVertex2f( x, y + 0.25f );
        }
        glEnd();

        if ( glGetError() != GL_NO_ERROR ) {
            g_glErr++;
        }
    }
    pBindFramebuffer( GL_FRAMEBUFFER_, 0 );
}

// The loader is resolved by hand, exactly as probe.cpp does it, so this builds
// with no .lib and runs beside a copy of openxr_loader.dll.
static HMODULE g_loader;
#define XRLOAD(n) PFN_##n n = (PFN_##n)GetProcAddress( g_loader, #n ); \
    if ( !n ) { Say( "FATAL: %s missing from openxr_loader.dll\n", #n ); return 9; }

int main( int argc, char **argv ) {
    const double seconds = ( argc > 1 ) ? atof( argv[1] ) : 30.0;
    XrResult r;

    // xrtiming.exe [seconds] [triangles per eye per frame]
    // 0 triangles is the old blank-frame behaviour; a few thousand is a real
    // scene's worth of fill and gives the encoder something it cannot predict.
    g_tris = ( argc > 2 ) ? atoi( argv[2] ) : 0;

    QueryPerformanceFrequency( &qpcFreq );

    g_out = fopen( "timing-report.txt", "w" );


    Say( "================================================================\n" );
    Say( " OpenXR frame timing probe - draws nothing, measures the runtime\n" );
    Say( "================================================================\n\n" );

    // ---------------------------------------------------------- GL context
    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA( NULL );
    wc.lpszClassName = "xrtiming";
    RegisterClassA( &wc );
    HWND hwnd = CreateWindowExA( 0, "xrtiming", "xrtiming", WS_OVERLAPPEDWINDOW,
                                 0, 0, 320, 240, NULL, NULL, wc.hInstance, NULL );
    HDC hdc = GetDC( hwnd );

    PIXELFORMATDESCRIPTOR pfd = { 0 };
    pfd.nSize = sizeof( pfd );
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat( hdc, &pfd );
    if ( !pf || !SetPixelFormat( hdc, pf, &pfd ) ) {
        Say( "FATAL: SetPixelFormat failed (%lu)\n", GetLastError() ); return 1;
    }
    HGLRC hglrc = wglCreateContext( hdc );
    if ( !hglrc || !wglMakeCurrent( hdc, hglrc ) ) {
        Say( "FATAL: wglCreateContext failed (%lu)\n", GetLastError() ); return 1;
    }
    Say( "  GL_RENDERER : %s\n", (const char *)glGetString( GL_RENDERER ) );
    Say( "  GL_VERSION  : %s\n\n", (const char *)glGetString( GL_VERSION ) );

    // ------------------------------------------------------------ instance
    g_loader = LoadLibraryA( "openxr_loader.dll" );
    if ( !g_loader ) {
        Say( "FATAL: openxr_loader.dll not found beside this exe.\n" );
        return 8;
    }
    XRLOAD( xrCreateInstance )
    XRLOAD( xrDestroyInstance )
    XRLOAD( xrGetInstanceProperties )
    XRLOAD( xrGetInstanceProcAddr )
    XRLOAD( xrGetSystem )
    XRLOAD( xrPollEvent )
    XRLOAD( xrCreateSession )
    XRLOAD( xrDestroySession )
    XRLOAD( xrBeginSession )
    XRLOAD( xrEndSession )
    XRLOAD( xrCreateReferenceSpace )
    XRLOAD( xrDestroySpace )
    XRLOAD( xrEnumerateViewConfigurationViews )
    XRLOAD( xrEnumerateSwapchainFormats )
    XRLOAD( xrCreateSwapchain )
    XRLOAD( xrDestroySwapchain )
    XRLOAD( xrEnumerateSwapchainImages )
    XRLOAD( xrAcquireSwapchainImage )
    XRLOAD( xrWaitSwapchainImage )
    XRLOAD( xrReleaseSwapchainImage )
    XRLOAD( xrWaitFrame )
    XRLOAD( xrBeginFrame )
    XRLOAD( xrEndFrame )
    XRLOAD( xrLocateViews )

    const char *exts[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo ici = { XR_TYPE_INSTANCE_CREATE_INFO };
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = exts;
    strcpy( ici.applicationInfo.applicationName, "xrtiming" );
    // XR_API_VERSION_1_0, not XR_CURRENT_API_VERSION. The bundled headers are
    // 1.1, and asking VirtualDesktopXR for 1.1 gets XR_ERROR_API_VERSION_
    // UNSUPPORTED (-4) before anything else happens. PreyVR itself asks for 1.0
    // - VrBase.c:159 - so the probe has to as well, or it is not measuring the
    // same session the game gets.
    ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;

    XrInstance instance = XR_NULL_HANDLE;
    r = xrCreateInstance( &ici, &instance );
    if ( XR_FAILED( r ) ) {
        // Decode it. Reading -4 as "no headset" cost a round trip: it is the
        // API version, and the headset was on the whole time.
        Say( "FATAL: xrCreateInstance failed (%d)\n", (int)r );
        Say( "         -2  runtime failure - is a headset actually streaming?\n" );
        Say( "         -4  API version unsupported\n" );
        Say( "         -6  initialization failed\n" );
        Say( "       anything else: no OpenXR runtime installed.\n" );
        return 2;
    }

    XrInstanceProperties ip = { XR_TYPE_INSTANCE_PROPERTIES };
    xrGetInstanceProperties( instance, &ip );
    Say( "  runtime     : %s\n", ip.runtimeName );

    XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId sysid = XR_NULL_SYSTEM_ID;
    r = xrGetSystem( instance, &sgi, &sysid );
    if ( XR_FAILED( r ) ) {
        Say( "FATAL: xrGetSystem failed (%d) - no headset streaming?\n", (int)r );
        return 3;
    }

    uint32_t nviews = 0;
    xrEnumerateViewConfigurationViews( instance, sysid,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &nviews, NULL );
    XrViewConfigurationView *vcv =
        (XrViewConfigurationView *)calloc( nviews, sizeof( *vcv ) );
    for ( uint32_t i = 0; i < nviews; i++ ) vcv[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    xrEnumerateViewConfigurationViews( instance, sysid,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, nviews, &nviews, vcv );
    Say( "  eye buffer  : %u x %u, %u views\n",
            vcv[0].recommendedImageRectWidth, vcv[0].recommendedImageRectHeight, nviews );

    PFN_xrGetOpenGLGraphicsRequirementsKHR pGetReq = NULL;
    xrGetInstanceProcAddr( instance, "xrGetOpenGLGraphicsRequirementsKHR",
                           (PFN_xrVoidFunction *)&pGetReq );
    if ( pGetReq ) {
        XrGraphicsRequirementsOpenGLKHR gr = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
        pGetReq( instance, sysid, &gr );     // required before xrCreateSession
    }

    XrGraphicsBindingOpenGLWin32KHR gb = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    gb.hDC = hdc;
    gb.hGLRC = hglrc;
    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next = &gb;
    sci.systemId = sysid;
    XrSession session = XR_NULL_HANDLE;
    r = xrCreateSession( instance, &sci, &session );
    if ( XR_FAILED( r ) ) { Say( "FATAL: xrCreateSession failed (%d)\n", (int)r ); return 4; }

    XrReferenceSpaceCreateInfo rsci = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    XrSpace space = XR_NULL_HANDLE;
    xrCreateReferenceSpace( session, &rsci, &space );

    // The same shape PreyVR asks for: one 2-layer array swapchain.
    uint32_t nfmt = 0;
    xrEnumerateSwapchainFormats( session, 0, &nfmt, NULL );
    int64_t *fmts = (int64_t *)calloc( nfmt ? nfmt : 1, sizeof( int64_t ) );
    xrEnumerateSwapchainFormats( session, nfmt, &nfmt, fmts );
    int64_t fmt = nfmt ? fmts[0] : GL_SRGB8_ALPHA8;
    for ( uint32_t i = 0; i < nfmt; i++ ) {
        if ( fmts[i] == GL_SRGB8_ALPHA8 ) { fmt = GL_SRGB8_ALPHA8; break; }
    }
    Say( "  swapchain   : format 0x%04X (runtime's first choice is 0x%04X)\n\n",
            (unsigned)fmt, nfmt ? (unsigned)fmts[0] : 0u );

    XrSwapchainCreateInfo scci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    scci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    scci.format = fmt;
    scci.sampleCount = 1;
    scci.width = vcv[0].recommendedImageRectWidth;
    scci.height = vcv[0].recommendedImageRectHeight;
    scci.faceCount = 1;
    scci.arraySize = 2;
    scci.mipCount = 1;
    XrSwapchain sc = XR_NULL_HANDLE;
    r = xrCreateSwapchain( session, &scci, &sc );
    if ( XR_FAILED( r ) ) { Say( "FATAL: xrCreateSwapchain failed (%d)\n", (int)r ); return 5; }

    uint32_t nimg = 0;
    xrEnumerateSwapchainImages( sc, 0, &nimg, NULL );
    XrSwapchainImageOpenGLKHR *imgs =
        (XrSwapchainImageOpenGLKHR *)calloc( nimg, sizeof( *imgs ) );
    for ( uint32_t i = 0; i < nimg; i++ ) imgs[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    xrEnumerateSwapchainImages( sc, nimg, &nimg, (XrSwapchainImageBaseHeader *)imgs );

    if ( g_tris > 0 ) {
        if ( !LoadGL() ) {
            Say( "  !! framebuffer entry points missing - running blank instead\n" );
            g_tris = 0;
        } else {
            pGenFramebuffers( 1, &g_fbo );
            Say( "  load        : %d triangles per eye per frame, new every frame\n", g_tris );
        }
    } else {
        Say( "  load        : none - blank frames\n" );
    }
    Say( "\n" );

    // --------------------------------------------------------- run the loop
    stat_t ivl   = { 0 }; ivl.name   = "submit interval";
    stat_t endF  = { 0 }; endF.name  = "xrEndFrame";
    stat_t waitF = { 0 }; waitF.name = "xrWaitFrame (blocks)";
    stat_t acq   = { 0 }; acq.name   = "acquire + wait image";
    stat_t slack = { 0 }; slack.name = "app time used";
    stat_t draw  = { 0 }; draw.name  = "drawing the load";

    bool running = false;
    int frames = 0, skipped = 0;
    double lastSubmit = 0.0, started = NowMs();
    XrTime lastDisplay = 0;
    double periodMs = 0.0;

    Say( "  running for %.0f seconds - put the headset on and leave it alone\n\n",
            seconds );

    while ( NowMs() - started < seconds * 1000.0 ) {
        XrEventDataBuffer ev = { XR_TYPE_EVENT_DATA_BUFFER };
        while ( xrPollEvent( instance, &ev ) == XR_SUCCESS ) {
            if ( ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED ) {
                const XrEventDataSessionStateChanged *ss =
                    (const XrEventDataSessionStateChanged *)&ev;
                if ( ss->state == XR_SESSION_STATE_READY ) {
                    XrSessionBeginInfo sbi = { XR_TYPE_SESSION_BEGIN_INFO };
                    sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    xrBeginSession( session, &sbi );
                    running = true;
                } else if ( ss->state == XR_SESSION_STATE_STOPPING ) {
                    xrEndSession( session );
                    running = false;
                }
            }
            ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        }
        if ( !running ) { Sleep( 5 ); continue; }

        XrFrameState fs = { XR_TYPE_FRAME_STATE };
        double t0 = NowMs();
        xrWaitFrame( session, NULL, &fs );
        double tWait = NowMs();
        Add( &waitF, tWait - t0 );
        if ( tWait - t0 > 40.0 ) NoteSpike( ( t0 - started ) / 1000.0, tWait - t0, "xrWaitFrame" );

        if ( lastDisplay ) {
            double dms = (double)( fs.predictedDisplayTime - lastDisplay ) / 1e6;
            Add( &ivl, dms );
            if ( dms > 40.0 ) NoteSpike( ( t0 - started ) / 1000.0, dms, "submit interval" );
        }
        lastDisplay = fs.predictedDisplayTime;

        XrFrameBeginInfo fbi = { XR_TYPE_FRAME_BEGIN_INFO };
        xrBeginFrame( session, &fbi );

        if ( !fs.shouldRender ) { skipped++; }

        double tAcq = NowMs();
        uint32_t idx = 0;
        XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        xrAcquireSwapchainImage( sc, &ai, &idx );
        XrSwapchainImageWaitInfo wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wi.timeout = XR_INFINITE_DURATION;
        xrWaitSwapchainImage( sc, &wi );
        Add( &acq, NowMs() - tAcq );

        // With no load this draws nothing and the image is whatever the runtime
        // left in it. With a load it is fresh noise in both eyes, which is what
        // the encoder has to deal with in a real session.
        double tDraw = NowMs();
        DrawLoad( imgs[ idx ].image, (int)scci.width, (int)scci.height, frames );
        Add( &draw, NowMs() - tDraw );

        XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage( sc, &ri );

        uint32_t nv = nviews;
        XrView *views = (XrView *)calloc( nv, sizeof( XrView ) );
        for ( uint32_t i = 0; i < nv; i++ ) views[i].type = XR_TYPE_VIEW;
        XrViewState vs = { XR_TYPE_VIEW_STATE };
        XrViewLocateInfo vli = { XR_TYPE_VIEW_LOCATE_INFO };
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = fs.predictedDisplayTime;
        vli.space = space;
        xrLocateViews( session, &vli, &vs, nv, &nv, views );

        XrCompositionLayerProjectionView pv[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW },
                                                   { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
        for ( uint32_t i = 0; i < nv && i < 2; i++ ) {
            pv[i].pose = views[i].pose;
            pv[i].fov  = views[i].fov;
            pv[i].subImage.swapchain = sc;
            pv[i].subImage.imageRect.extent.width  = (int32_t)scci.width;
            pv[i].subImage.imageRect.extent.height = (int32_t)scci.height;
            pv[i].subImage.imageArrayIndex = i;
        }
        XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        layer.space = space;
        layer.viewCount = nv < 2 ? nv : 2;
        layer.views = pv;
        const XrCompositionLayerBaseHeader *layers[1] =
            { (const XrCompositionLayerBaseHeader *)&layer };

        XrFrameEndInfo fei = { XR_TYPE_FRAME_END_INFO };
        fei.displayTime = fs.predictedDisplayTime;
        fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fei.layerCount = fs.shouldRender ? 1 : 0;
        fei.layers = fs.shouldRender ? layers : NULL;

        double tEnd = NowMs();
        Add( &slack, tEnd - tWait );          // everything we did with the frame
        xrEndFrame( session, &fei );
        {
            double e = NowMs() - tEnd;
            Add( &endF, e );
            if ( e > 40.0 ) NoteSpike( ( tEnd - started ) / 1000.0, e, "xrEndFrame" );
        }

        free( views );

        double now = NowMs();
        if ( lastSubmit > 0.0 ) { /* interval measured from display times above */ }
        lastSubmit = now;
        frames++;
    }

    double elapsed = ( NowMs() - started ) / 1000.0;

    // The display period is the MEDIAN interval, not the first one seen. Taking
    // the first sample reported 9.75 ms / 102.6 Hz on a run whose p50 was
    // plainly 11.10 ms / 90 Hz, and every "over one display period" figure
    // below was then measured against a period that did not exist.
    periodMs = Pct( &ivl, 50 );

    Say( "----------------------------------------------------------------\n" );
    Say( " %d frames in %.1f s = %.1f fps", frames, elapsed, frames / elapsed );
    if ( periodMs > 0.0 ) Say( "   (display period %.2f ms = %.1f Hz)", periodMs, 1000.0 / periodMs );
    Say( "\n" );
    if ( skipped ) Say( " %d frames the runtime asked us not to render\n", skipped );
    Say( "----------------------------------------------------------------\n\n" );

    Report( &ivl,   0.0 );
    Report( &waitF, 0.0 );
    Report( &acq,   periodMs );
    if ( g_tris > 0 ) {
        Report( &draw, periodMs );
        Say( "  load actually drew   : %s", ( g_fboBad || g_glErr ) ? "NO - " : "yes" );
        if ( g_fboBad ) Say( "framebuffer incomplete on %d frames ", g_fboBad );
        if ( g_glErr )  Say( "GL rejected the draw on %d frames ", g_glErr );
        Say( "\n\n" );
    }
    Report( &slack, periodMs );
    Report( &endF,  periodMs );

    if ( g_nspikes > 0 ) {
        Say( "Stalls over 40 ms - when, how long, and what we were inside:\n" );
        for ( int i = 0; i < g_nspikes; i++ ) {
            Say( "    %02d:%02d:%02d.%03d   t=%7.2f s   %7.2f ms   %s",
                 g_spikes[i].wall.wHour, g_spikes[i].wall.wMinute,
                 g_spikes[i].wall.wSecond, g_spikes[i].wall.wMilliseconds,
                 g_spikes[i].at, g_spikes[i].ms, g_spikes[i].where );
            if ( i > 0 ) Say( "      (%.2f s since the last one)", g_spikes[i].at - g_spikes[i-1].at );
            Say( "\n" );
        }
        Say( "  Evenly spaced means something wakes on a timer. Scattered means\n" );
        Say( "  something is competing for the machine.\n" );
        Say( "\n  The clock times are local, so they can be looked up in Event Viewer\n" );
        Say( "  or Reliability Monitor - that is what can actually NAME the thing.\n\n" );
    } else {
        Say( "No stall over 40 ms this run.\n\n" );
    }

    Say( "How to read this:\n" );
    Say( "  Nothing was drawn. \"app time used\" is this probe doing almost no\n" );
    Say( "  work, so it should be a fraction of a millisecond. If xrEndFrame is\n" );
    Say( "  costing milliseconds here, that cost is the runtime, the driver or\n" );
    Say( "  the link - no game can avoid it.\n\n" );
    Say( "  Change one thing between runs and compare.\n" );

    xrDestroySwapchain( sc );
    xrDestroySpace( space );
    if ( running ) xrEndSession( session );
    xrDestroySession( session );
    xrDestroyInstance( instance );
    if ( g_out ) {
        fclose( g_out );
        g_out = NULL;
        Say( "\nSaved to timing-report.txt beside this exe.\n" );
    }
    return 0;
}
