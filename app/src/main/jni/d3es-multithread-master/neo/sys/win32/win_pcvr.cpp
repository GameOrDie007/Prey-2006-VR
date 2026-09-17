/*
===========================================================================
PCVR: Windows half of the Sys_* API.

This fork's POSIX layer grew a set of Sys_* functions that sys/win32 never
gained, because nobody had built this tree on Windows. Measured against
sys/sys_public.h, eighteen functions are declared and defined nowhere in the
Windows source set - this file supplies them.

Almost all of them are stubs on the platform he actually plays, so the values
here are copied from theirs rather than invented:

    Sys_AlreadyRunning        posix_main.cpp      return false
    Sys_DoPreferences         linux/main.cpp      {}
    Sys_ShutdownSymbols       linux/main.cpp      {}
    Sys_GetCallStack          linux/main.cpp      zero the array
    Sys_GetCallStackStr       linux/main.cpp      ""
    Sys_GetCallStackCurStr    linux/main.cpp      ""
    Sys_FPU_EnableExceptions  linux/main.cpp      {}
    Sys_FPU_GetState          posix_main.cpp      ""
    Sys_FPU_StackIsEmpty      posix_main.cpp      true
    Sys_GetProcessorString    linux/main.cpp      "generic"
    Sys_DefaultBasePath       linux/main.cpp      the working directory
    Sys_DefaultSavePath       linux/main.cpp      the working directory
    Sys_DefaultCDPath         linux/main.cpp      ""

"generic" is worth noting: it is what his build reports, and it agrees with
idSIMD_Generic being the SIMD processor selected on both platforms after the
Simd_* guards were narrowed in Milestone 2.

The two that are not stubs are the clock pair. Theirs reads the timestamp
counter directly and parses /proc/cpuinfo for the frequency; the Win32
equivalent is QueryPerformanceCounter/Frequency, which is what these callers
want - Sys_GetClockTicks feeds idTimer and the profiling paths only.
===========================================================================
*/

#include "idlib/precompiled.h"
#include "sys/win32/win_local.h"

#include <dbghelp.h>	// PCVR: the crash handler at the bottom of this file
#include <tlhelp32.h>	// PCVR: the thread list, for the freeze watchdog
#include <intrin.h>	// PCVR: __cpuid, for the processor name in the log
#include <dxgi.h>	// PCVR: Sys_GetVideoRam - loaded dynamically, never linked

#include <direct.h>

/*
================
Sys_AlreadyRunning
================
*/
bool Sys_AlreadyRunning( void ) {
	return false;
}

/*
================
Sys_DoPreferences
================
*/
void Sys_DoPreferences( void ) {
}

/*
================
Sys_SetFatalError
================
*/
static char fatalError[ 1024 ];

void Sys_SetFatalError( const char *error ) {
	idStr::Copynz( fatalError, error, sizeof( fatalError ) );
}

/*
================
Sys_GetClockTicks / Sys_ClockTicksPerSecond

Theirs uses rdtsc plus /proc/cpuinfo. QueryPerformanceCounter is the Win32
equivalent and is monotonic, which rdtsc on a modern multi-core part is not.
================
*/
double Sys_GetClockTicks( void ) {
	LARGE_INTEGER li;

	QueryPerformanceCounter( &li );
	return (double)li.QuadPart;
}

double Sys_ClockTicksPerSecond( void ) {
	static double ret = 0.0;

	if ( ret == 0.0 ) {
		LARGE_INTEGER li;
		QueryPerformanceFrequency( &li );
		ret = (double)li.QuadPart;
	}

	return ret;
}

/*
================
Sys_GetProcessorString
================
*/
const char *Sys_GetProcessorString( void ) {
	return "generic";
}

/*
================
FPU state. All stubs on his build.
================
*/
bool Sys_FPU_StackIsEmpty( void ) {
	return true;
}

const char *Sys_FPU_GetState( void ) {
	return "";
}

void Sys_FPU_EnableExceptions( int exceptions ) {
}

/*
================
Call stacks. Stubs on his build, so stubs here - a real StackWalk64
implementation would be a PC-branch improvement, not a 1:1 port.
================
*/
void Sys_GetCallStack( address_t *callStack, const int callStackSize ) {
	for ( int i = 0; i < callStackSize; i++ ) {
		callStack[ i ] = 0;
	}
}

const char *Sys_GetCallStackStr( const address_t *callStack, const int callStackSize ) {
	return "";
}

const char *Sys_GetCallStackCurStr( int depth ) {
	return "";
}

const char *Sys_GetCallStackCurAddressStr( int depth ) {
	return "";
}

void Sys_ShutdownSymbols( void ) {
}

/*
================
Paths.

Theirs returns getcwd() for both base and save paths, and "" for the CD path.
Keeping that shape: a PC build launched from the game directory then behaves
the way his does, and the data lookup rules stay identical.
================
*/
static const char *workdir( void ) {
	static char wd[ 256 ];

	if ( !_getcwd( wd, sizeof( wd ) ) ) {
		wd[ 0 ] = '\0';
	}

	return wd;
}

const char *Sys_DefaultBasePath( void ) {
	return workdir();
}

const char *Sys_DefaultSavePath( void ) {
	return workdir();
}

const char *Sys_DefaultCDPath( void ) {
	return "";
}

/*
================
Sys_EXEPath

Theirs resolves /proc/<pid>/exe. GetModuleFileName is the direct equivalent.
================
*/
const char *Sys_EXEPath( void ) {
	static char buf[ 1024 ];

	if ( !GetModuleFileNameA( NULL, buf, sizeof( buf ) - 1 ) ) {
		buf[ 0 ] = '\0';
	}

	return buf;
}

/*
================
VR_Doom3Main

PCVR: their sys/linux/main.cpp has exactly this, wrapping main_android().
AppThreadFunction calls it after the GL context and the XR session exist, so
it is the point where the VR layer hands control to the engine.

Their version also sets native_library_dir from $GAMELIBDIR before the call.
That is for the Android .so lookup path; on Windows FindDLL looks beside the
executable, so there is nothing to set.
================
*/
// ---------------------------------------------------------------------------
// PCVR: flatscreen input.
//
// The VR fork replaced the engine's Win32 input with its own controller layer,
// so win_input.cpp is now only scan-code tables and Win_MapKey, and
// MouseMove()/JoystickMove() exist but are never called. What survived is a
// clean injection API in sys/events.cpp - Sys_AddKeyEvent and friends - which
// the VR layer feeds. Flat mode feeds the same three from window messages, so
// nothing downstream of them needs to know which mode it is in.
//
// Mouse look is done by recentring rather than by accumulating WM_MOUSEMOVE:
// the cursor is warped back to the middle of the client area every frame and
// the delta from the middle is the movement. That is the classic approach and
// it does not stop at the edge of the screen.
// ---------------------------------------------------------------------------

extern "C" void Sys_AddKeyEvent( int key, bool pressed );
extern "C" void Sys_AddCharEvent( int ch );
extern "C" void Sys_AddMouseMoveEvent( int dx, int dy );
extern "C" void Sys_AddMouseButtonEvent( int button, bool pressed );

idCVar pcvr_mouseInfo( "pcvr_mouseInfo", "0", CVAR_SYSTEM | CVAR_BOOL,
		"PCVR: log the flatscreen mouse capture state once a second" );

// Up here rather than further down the file: the window procedure below
// uses it to keep the mouse out of VR, and a declaration after the first
// use does not compile.
extern "C" bool PCVR_Flatscreen( void );   // Doom3Quest_SurfaceView.c

static bool pcvr_mouseCaptured = false;
static HWND pcvr_inputHWND = NULL;

static void PCVR_CaptureMouse( HWND hwnd, bool capture ) {
	if ( capture == pcvr_mouseCaptured ) {
		return;
	}
	pcvr_mouseCaptured = capture;
	ShowCursor( capture ? FALSE : TRUE );
	if ( capture ) {
		SetCapture( hwnd );
	} else {
		ReleaseCapture();
	}
}

extern "C" LRESULT CALLBACK PCVR_FlatWndProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ) {
	pcvr_inputHWND = hwnd;

	switch ( msg ) {
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
			// bit 30 of lParam is the previous key state; ignoring repeats
			// keeps the engine's own auto-repeat handling in charge.
			if ( ( lParam & ( 1 << 30 ) ) == 0 ) {
				Sys_AddKeyEvent( Win_MapKey( (int)lParam ), true );
			}
			return 0;

		case WM_KEYUP:
		case WM_SYSKEYUP:
			Sys_AddKeyEvent( Win_MapKey( (int)lParam ), false );
			return 0;

		case WM_CHAR:
			Sys_AddCharEvent( (int)wParam );
			return 0;

		// The mouse is flat mode's alone. In VR the pointer belongs to the
		// desktop: capturing it would hide it and lock it to a window the
		// player is not looking at, and a click would drive a menu nobody can
		// see. Leaving the capture alone also keeps PCVR_FlatPumpMouse inert,
		// because it returns early unless a capture happened.
		case WM_LBUTTONDOWN: if ( PCVR_Flatscreen() ) { Sys_AddMouseButtonEvent( 1, true );  return 0; } break;
		case WM_LBUTTONUP:   if ( PCVR_Flatscreen() ) { Sys_AddMouseButtonEvent( 1, false ); return 0; } break;
		case WM_RBUTTONDOWN: if ( PCVR_Flatscreen() ) { Sys_AddMouseButtonEvent( 2, true );  return 0; } break;
		case WM_RBUTTONUP:   if ( PCVR_Flatscreen() ) { Sys_AddMouseButtonEvent( 2, false ); return 0; } break;
		case WM_MBUTTONDOWN: if ( PCVR_Flatscreen() ) { Sys_AddMouseButtonEvent( 3, true );  return 0; } break;
		case WM_MBUTTONUP:   if ( PCVR_Flatscreen() ) { Sys_AddMouseButtonEvent( 3, false ); return 0; } break;

		case WM_SETFOCUS:
			if ( PCVR_Flatscreen() ) {
				PCVR_CaptureMouse( hwnd, true );
				return 0;
			}
			break;

		case WM_KILLFOCUS:
			if ( PCVR_Flatscreen() ) {
				PCVR_CaptureMouse( hwnd, false );
				return 0;
			}
			break;

		case WM_CLOSE:
			// Through the engine's own quit, which since Milestone 12 actually
			// shuts things down in order. Never PostQuitMessage here.
			cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
			return 0;
	}

	return DefWindowProcA( hwnd, msg, wParam, lParam );
}

// PCVR: this MUST run on the thread that created the window.
//
// Windows message queues are per-thread: PeekMessage only ever returns
// messages for windows created by the calling thread. The window is created in
// ovrEgl_CreateContext, which main() reaches directly, so it belongs to the
// main thread - but GLimp_SwapBuffers, where the pump first lived, is called
// from idRenderSystemLocal::BackendThread. The pump therefore ran on a thread
// with no windows and drained an empty queue forever: the window proc never
// executed, and a synthesised keypress went nowhere. Rendering was unaffected,
// which is what made it look like an input bug rather than a threading one.
//
// idCommonLocal::Frame is on the main thread, so the pump lives there now.
// ---------------------------------------------------------------------------
// PCVR: flatscreen gamepad, through XInput.
//
// JoystickMove() exists in UsercmdGen and is never called, but it is also not
// what a pad wants: it turns the view with the stick unless strafe is held,
// which is 2004 behaviour. Flat mode maps twin-stick instead - left stick
// moves, right stick looks - and routes buttons through Sys_AddKeyEvent as
// K_JOY1.. so they go through the normal bind system.
//
// XInput is loaded by name rather than linked, so a machine without it, or
// without a pad, costs nothing and fails quietly. Polling happens on the main
// thread inside PCVR_FlatPumpWindow, beside the keyboard and mouse, because
// button edges become engine events and that is where the other injectors run.
// ---------------------------------------------------------------------------

typedef struct {
	unsigned long dwPacketNumber;
	unsigned short wButtons;
	unsigned char  bLeftTrigger;
	unsigned char  bRightTrigger;
	short sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} PCVR_XINPUT_STATE;

typedef unsigned long (__stdcall *PFN_XInputGetState)( unsigned long, PCVR_XINPUT_STATE* );

#define PCVR_PAD_BUTTONS 14
static const unsigned short pcvr_padBits[PCVR_PAD_BUTTONS] = {
	0x0001, 0x0002, 0x0004, 0x0008,   // dpad up down left right
	0x0010, 0x0020,                   // start back
	0x0040, 0x0080,                   // thumb clicks
	0x0100, 0x0200,                   // shoulders
	0x1000, 0x2000, 0x4000, 0x8000,   // A B X Y
};

// Same order as pcvr_padBits, then the two triggers. Printed on every edge so
// a mapping can be read out of a log instead of remembered.
static const char *pcvr_padNames[PCVR_PAD_BUTTONS + 2] = {
	"DPadUp", "DPadDown", "DPadLeft", "DPadRight",
	"Start", "Back",
	"LeftThumb", "RightThumb",
	"LeftShoulder", "RightShoulder",
	"A", "B", "X", "Y",
	"LeftTrigger", "RightTrigger",
};

static PFN_XInputGetState pcvr_XInputGetState = NULL;
static bool  pcvr_padChecked = false;
static bool  pcvr_padPresent = false;
static bool  pcvr_padWas[PCVR_PAD_BUTTONS + 2];   // + the two triggers
static float pcvr_padMoveX, pcvr_padMoveY, pcvr_padLookX, pcvr_padLookY;

idCVar pcvr_padDeadzone( "pcvr_padDeadzone", "0.20", CVAR_SYSTEM | CVAR_FLOAT,
		"PCVR: flatscreen gamepad stick deadzone, 0 to 1" );
idCVar pcvr_padLookSpeed( "pcvr_padLookSpeed", "150", CVAR_SYSTEM | CVAR_FLOAT,
		"PCVR: flatscreen gamepad look speed, degrees per second at full stick" );
idCVar pcvr_padInfo( "pcvr_padInfo", "0", CVAR_SYSTEM | CVAR_BOOL,
		"PCVR: log gamepad axes and buttons every second, to check a mapping without guessing" );

static float PCVR_PadAxis( short raw, float dead ) {
	float v = (float)raw / 32767.0f;
	if ( v > 1.0f )  v = 1.0f;
	if ( v < -1.0f ) v = -1.0f;
	if ( v > -dead && v < dead ) {
		return 0.0f;
	}
	// rescale outside the deadzone so the first movement past it is not a jump
	const float sign = v < 0.0f ? -1.0f : 1.0f;
	return sign * ( ( ( v < 0.0f ? -v : v ) - dead ) / ( 1.0f - dead ) );
}

static void PCVR_PollGamepad( void ) {
	if ( !pcvr_padChecked ) {
		pcvr_padChecked = true;
		const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
		for ( int i = 0; i < 3 && !pcvr_XInputGetState; i++ ) {
			HMODULE h = LoadLibraryA( dlls[i] );
			if ( h ) {
				pcvr_XInputGetState =
						(PFN_XInputGetState)GetProcAddress( h, "XInputGetState" );
				if ( pcvr_XInputGetState ) {
					common->Printf( "PCVR: gamepad support via %s\n", dlls[i] );
				}
			}
		}
		if ( !pcvr_XInputGetState ) {
			common->Printf( "PCVR: no XInput on this machine; gamepad disabled\n" );
		}
	}

	pcvr_padMoveX = pcvr_padMoveY = pcvr_padLookX = pcvr_padLookY = 0.0f;
	if ( !pcvr_XInputGetState ) {
		return;
	}

	PCVR_XINPUT_STATE st;
	memset( &st, 0, sizeof( st ) );
	if ( pcvr_XInputGetState( 0, &st ) != 0 ) {          // ERROR_SUCCESS == 0
		if ( pcvr_padPresent ) {
			common->Printf( "PCVR: gamepad disconnected\n" );
			pcvr_padPresent = false;
			memset( pcvr_padWas, 0, sizeof( pcvr_padWas ) );
		}
		return;
	}
	if ( !pcvr_padPresent ) {
		common->Printf( "PCVR: gamepad connected\n" );
		pcvr_padPresent = true;
	}

	const float dead = pcvr_padDeadzone.GetFloat();
	pcvr_padMoveX = PCVR_PadAxis( st.sThumbLX, dead );
	pcvr_padMoveY = PCVR_PadAxis( st.sThumbLY, dead );
	pcvr_padLookX = PCVR_PadAxis( st.sThumbRX, dead );
	pcvr_padLookY = PCVR_PadAxis( st.sThumbRY, dead );

	// Buttons as edges, so a held button is one press and one release.
	for ( int i = 0; i < PCVR_PAD_BUTTONS; i++ ) {
		const bool down = ( st.wButtons & pcvr_padBits[i] ) != 0;
		if ( down != pcvr_padWas[i] ) {
			pcvr_padWas[i] = down;
			Sys_AddKeyEvent( K_JOY1 + i, down );
			if ( pcvr_padInfo.GetBool() ) {
				common->Printf( "PCVR pad edge: JOY%d %-13s %s\n",
						i + 1, pcvr_padNames[i], down ? "down" : "up" );
			}
		}
	}
	// Triggers are analog; treat them as buttons past the halfway point.
	const bool lt = st.bLeftTrigger  > 127;
	const bool rt = st.bRightTrigger > 127;
	for ( int t = 0; t < 2; t++ ) {
		const bool on = t == 0 ? lt : rt;
		const int idx = PCVR_PAD_BUTTONS + t;
		if ( on != pcvr_padWas[idx] ) {
			pcvr_padWas[idx] = on;
			Sys_AddKeyEvent( K_JOY1 + idx, on );
			if ( pcvr_padInfo.GetBool() ) {
				common->Printf( "PCVR pad edge: JOY%d %-13s %s\n",
						idx + 1, pcvr_padNames[idx], on ? "down" : "up" );
			}
		}
	}

	if ( pcvr_padInfo.GetBool() ) {
		static int last = 0;
		const int now = Sys_Milliseconds();
		if ( now - last > 1000 ) {
			last = now;
			common->Printf( "PCVR pad: move %.2f %.2f  look %.2f %.2f  "
					"buttons 0x%04X  triggers %d %d\n",
					pcvr_padMoveX, pcvr_padMoveY, pcvr_padLookX, pcvr_padLookY,
					(unsigned)st.wButtons, (int)st.bLeftTrigger, (int)st.bRightTrigger );
		}
	}
}

// Read by idUsercmdGenLocal::MakeCurrent in flat mode.
extern "C" void PCVR_FlatPadState( float *moveX, float *moveY,
		float *lookX, float *lookY ) {
	if ( moveX ) *moveX = pcvr_padMoveX;
	if ( moveY ) *moveY = pcvr_padMoveY;
	if ( lookX ) *lookX = pcvr_padLookX;
	if ( lookY ) *lookY = pcvr_padLookY;
}

// ---------------------------------------------------------------------------
// PCVR: frame counter, so an unattended run can answer what a setting costs.
//
// Counts only frames after the first one following a gap of more than a
// second, so a map load does not drag the average down. Reported once, at
// quit, next to pcvr_autoQuit's own line.
// ---------------------------------------------------------------------------
static int pcvr_fpsFrames = 0;
static int pcvr_fpsFirstMs = 0;
static int pcvr_fpsLastMs = 0;

// PCVR: an average cannot tell a steady 85 from a locked 90 that drops a frame
// a second, and those are different faults with different causes. So keep the
// distribution of frame intervals, not just the count.
//
// Buckets are milliseconds, 0..63. At 90Hz a frame is 11.1ms, so a healthy run
// piles up in bucket 11 with nothing above it; a beat against the compositor
// shows as a second pile around 22; a genuine deficit shows as a spread.
#define PCVR_MAX_MS 64
static int pcvr_hist[PCVR_MAX_MS];
static int pcvr_worstMs = 0;

// Defined with PCVR_NoteLocate further down. The run report below prints them,
// and so does the freeze report: whether the compositor was told about the same
// head pose the world was drawn from.
extern long	pcvr_ticsPerFrame[ 4 ];
extern long	pcvr_ticsPerFrameMore;
extern long	pcvr_poseLagHist[ 4 ];
extern long	pcvr_poseLagWorse;
void PCVR_ReportPoseAge( void );			// the judder measurement, printed with the rest
void PCVR_ReportHaptics( void );			// the one OpenXR call still on the game thread
void PCVR_ReportSlack( void );				// how close to the deadline we submit
void PCVR_ReportXrCalls( void );			// what the runtime does with our frame
void PCVR_ReportPortals( int frames );		// the cost of drawing every portal
void PCVR_ReportMirror( void );				// what the desktop mirror cost
extern "C" int PCVR_MirrorSkips( void );	// mirror frames the pacer skipped
extern "C" void PCVR_MirrorStats( double* blitUs, double* swapUs, int* timed,
		int* asked, int* ret, int* readBack );
static void PCVR_Defer( const char *line );	// queue a line for the main thread
extern "C" void PCVR_ResetPortalFrame( void );

// Defined with the watchdog at the bottom of this file. These two are all
// PCVR_NoteFrame borrows from it: a counter to bump, and the test switch.
extern volatile LONG	pcvr_frameSerial;
extern volatile int		pcvr_frameLockedActive;
extern idCVar			pcvr_freezeTest;

// PCVR: how many game ticks landed inside each rendered frame.
//
// The game ticks on the async thread and the frames are presented by the
// compositor, and the two are only as aligned as the tick period makes them. A
// frame that consumes two ticks advances the world twice in one displayed
// image, which is a hitch in everything moving relative to the player - and a
// frame that consumes none shows the previous world state again. Either reads
// as judder, and neither shows up in a frame-time histogram, which is why the
// frame rate has looked healthy throughout.
//
// Needs no head movement to measure, so it works on the desk as well as in a
// headset.
volatile long	pcvr_ticCount = 0;
static long		pcvr_ticAtLastFrame = 0;
long			pcvr_ticsPerFrame[ 4 ];
long			pcvr_ticsPerFrameMore = 0;

extern "C" void PCVR_NoteTic( void ) {
	InterlockedIncrement( &pcvr_ticCount );
}

extern "C" void PCVR_NoteFrame( void ) {
	const int now = Sys_Milliseconds();

	{
		const long ticks = pcvr_ticCount - pcvr_ticAtLastFrame;

		pcvr_ticAtLastFrame = pcvr_ticCount;
		if ( ticks >= 0 && ticks < 4 ) {
			pcvr_ticsPerFrame[ ticks ]++;
		} else {
			pcvr_ticsPerFrameMore++;
		}
	}

	// The watchdog reads this and nothing else: a number that stops moving is
	// the whole definition of a freeze, and it costs one interlocked add.
	InterlockedIncrement( &pcvr_frameSerial );
	PCVR_ResetPortalFrame();

	// PCVR: the heartbeat the frame-locked tic runs on. A displayed frame is
	// the only clock in this process that agrees with the display, which is
	// the entire point - see pcvr_frameLockedTics in Common.cpp. Triggering an
	// event nobody is waiting on is free, so this costs nothing when off.
	if ( pcvr_frameLockedActive ) {
		Sys_TriggerEvent( TRIGGER_EVENT_VR_FRAME );
	}

	// The positive control. Setting pcvr_freezeTest to a number of seconds
	// stalls this thread once, on purpose, so the watchdog can be seen to fire
	// and its report can be read before anyone needs it in anger.
	if ( pcvr_freezeTest.GetInteger() > 0 ) {
		const int secs = pcvr_freezeTest.GetInteger();

		pcvr_freezeTest.SetInteger( 0 );
		common->Printf( "PCVR: pcvr_freezeTest - stalling this thread for %d seconds\n", secs );
		Sleep( (DWORD)secs * 1000 );
		common->Printf( "PCVR: pcvr_freezeTest - resumed\n" );
	}

	if ( pcvr_fpsLastMs == 0 || now - pcvr_fpsLastMs > 1000 ) {
		// a load, or the first frame - restart the measurement
		pcvr_fpsFirstMs = now;
		pcvr_fpsFrames = 0;
		pcvr_worstMs = 0;
		memset( pcvr_hist, 0, sizeof( pcvr_hist ) );
	} else {
		int d = now - pcvr_fpsLastMs;
		if ( d < 0 ) {
			d = 0;
		}
		if ( d > pcvr_worstMs ) {
			pcvr_worstMs = d;
		}
		pcvr_hist[ d < PCVR_MAX_MS ? d : PCVR_MAX_MS - 1 ]++;
	}
	pcvr_fpsLastMs = now;
	pcvr_fpsFrames++;
}

extern "C" void PCVR_ReportFrames( void ) {
	const int span = pcvr_fpsLastMs - pcvr_fpsFirstMs;
	if ( pcvr_fpsFrames <= 1 || span <= 0 ) {
		common->Printf( "PCVR frames: not enough frames to average\n" );
		return;
	}

	const float fps = ( pcvr_fpsFrames * 1000.0f ) / (float)span;
	common->Printf( "PCVR frames: %d in %d ms = %.1f fps average\n",
			pcvr_fpsFrames, span, fps );

	int total = 0;
	for ( int i = 0; i < PCVR_MAX_MS; i++ ) {
		total += pcvr_hist[i];
	}
	if ( total < 10 ) {
		return;
	}

	// percentiles over the interval distribution
	const int wantP[3] = { 50, 95, 99 };
	int pIdx = 0;
	int running = 0;
	int pVal[3] = { 0, 0, 0 };
	for ( int i = 0; i < PCVR_MAX_MS && pIdx < 3; i++ ) {
		running += pcvr_hist[i];
		while ( pIdx < 3 && running * 100 >= total * wantP[pIdx] ) {
			pVal[pIdx++] = i;
		}
	}

	// how many frames took longer than one and a half display periods
	const int refresh = renderSystem ? renderSystem->GetRefresh() : 90;
	const int periodMs = refresh > 0 ? ( 1000 / refresh ) : 11;
	const int lateAt = ( periodMs * 3 ) / 2 + 1;
	int late = 0;
	for ( int i = lateAt; i < PCVR_MAX_MS; i++ ) {
		late += pcvr_hist[i];
	}

	common->Printf( "PCVR frametime: median %d ms  p95 %d ms  p99 %d ms  worst %d ms\n",
			pVal[0], pVal[1], pVal[2], pcvr_worstMs );
	common->Printf( "PCVR late frames: %d of %d (%.2f%%) took >= %d ms "
			"(refresh %d Hz, period %d ms)\n",
			late, total, ( 100.0f * late ) / (float)total, lateAt, refresh, periodMs );

	// the histogram itself, because a shape is worth more than three numbers
	common->Printf( "PCVR histogram (ms: count):" );

	for ( int i = 0; i < PCVR_MAX_MS; i++ ) {
		if ( pcvr_hist[i] > 0 ) {
			common->Printf( " %d:%d", i, pcvr_hist[i] );
		}
	}
	common->Printf( "\n" );

	// The judder question, in the ordinary run report rather than only in a
	// freeze: 0 means the compositor was told about the same head pose the
	// world was actually drawn from.
	common->Printf( "PCVR pose agreement (0 = layer matches the picture): "
					"0:%ld 1:%ld 2:%ld 3:%ld more:%ld\n",
					pcvr_poseLagHist[ 0 ], pcvr_poseLagHist[ 1 ],
					pcvr_poseLagHist[ 2 ], pcvr_poseLagHist[ 3 ], pcvr_poseLagWorse );

	// Game ticks per rendered frame. 1 everywhere is what smooth looks like;
	// a tail at 0 or 2 is the world advancing unevenly against the display,
	// which a frame-time histogram cannot see.
	common->Printf( "PCVR ticks per frame (1 is even): 0:%ld 1:%ld 2:%ld 3:%ld more:%ld\n",
					pcvr_ticsPerFrame[ 0 ], pcvr_ticsPerFrame[ 1 ],
					pcvr_ticsPerFrame[ 2 ], pcvr_ticsPerFrame[ 3 ],
					pcvr_ticsPerFrameMore );

	PCVR_ReportPoseAge();
	PCVR_ReportHaptics();
	PCVR_ReportSlack();
	PCVR_ReportXrCalls();
	PCVR_ReportPortals( total );
	PCVR_ReportMirror();

	// PCVR: what this run actually WAS, printed by the run itself.
	//
	// A test bat passes +set r_multithread 0 and an archived seta in the config
	// can put it back, so a log that is supposed to be the single-threaded arm
	// can be the threaded one with nothing to show for it. The arm has to be
	// read off the running engine, not off the command that launched it.
	common->Printf( "PCVR arm: r_multithread %d  evenTics %d  frameLockedTics %d  supersampling %.2f  refresh %d Hz  layerMode %d  msaa %.0f\n",
			cvarSystem->GetCVarInteger( "r_multithread" ),
			cvarSystem->GetCVarInteger( "pcvr_evenTics" ),
			cvarSystem->GetCVarInteger( "pcvr_frameLockedTics" ),
			cvarSystem->GetCVarFloat( "vr_supersampling" ),
			renderSystem ? renderSystem->GetRefresh() : 0,
			cvarSystem->GetCVarInteger( "pcvr_layerMode" ),
			cvarSystem->GetCVarFloat( "vr_msaa" ) );
}

extern "C" void PCVR_FlatPumpMouse( void );
static void PCVR_PollGamepad( void );

extern "C" void PCVR_FlatPumpWindow( void ) {
	MSG msg;
	while ( PeekMessageA( &msg, NULL, 0, 0, PM_REMOVE ) ) {
		TranslateMessage( &msg );
		DispatchMessageA( &msg );
	}

	PCVR_FlatPumpMouse();
	PCVR_PollGamepad();
}

extern "C" void PCVR_FlatPumpMouse( void ) {
	// PCVR: why the mouse is doing nothing, when it is doing nothing.
	//
	// Mouse look and the menu cursor both died in flat mode and there was no
	// way to tell which of the three gates below was shut. One line a second,
	// off by default, beats another evening of reading this function.
	static int pcvr_mouseLastReport = 0;
	if ( pcvr_mouseInfo.GetBool() ) {
		const int now = Sys_Milliseconds();
		if ( now - pcvr_mouseLastReport > 1000 ) {
			pcvr_mouseLastReport = now;
			common->Printf( "PCVR mouse: captured %d, hwnd %p, foreground %p%s\n",
					pcvr_mouseCaptured ? 1 : 0, (void *)pcvr_inputHWND,
					(void *)GetForegroundWindow(),
					( GetForegroundWindow() == pcvr_inputHWND ) ? "" : "  <- not ours" );
		}
	}

	if ( !pcvr_mouseCaptured || !pcvr_inputHWND ) {
		return;
	}
	if ( GetForegroundWindow() != pcvr_inputHWND ) {
		PCVR_CaptureMouse( pcvr_inputHWND, false );
		return;
	}

	RECT client;
	GetClientRect( pcvr_inputHWND, &client );

	POINT centre;
	centre.x = ( client.right - client.left ) / 2;
	centre.y = ( client.bottom - client.top ) / 2;
	ClientToScreen( pcvr_inputHWND, &centre );

	POINT now;
	if ( !GetCursorPos( &now ) ) {
		return;
	}

	const int dx = now.x - centre.x;
	const int dy = now.y - centre.y;
	if ( dx != 0 || dy != 0 ) {
		Sys_AddMouseMoveEvent( dx, dy );
		SetCursorPos( centre.x, centre.y );
	}
}

int main_win( int argc, char *argv[] );   // defined in win_main.cpp, C++ linkage

extern "C" void VR_Doom3Main( int argc, const char **argv ) {
	main_win( argc, (char **)argv );
}

/*
================
Analog input globals

PCVR: declared extern by framework/UsercmdGen.cpp and defined at the bottom of
their sys/linux/main.cpp - the platform file, so Windows had no copy. Same
initial values as theirs.
================
*/
float analogx = 0.0f;
float analogy = 0.0f;
int analogenabled = 0;

/*
================
PCVR: a crash handler.

Sys_GetCallStack and friends above are stubs, copied from theirs, because a
Quest build has nowhere to show a call stack. On Windows that leaves a crash
reporting nothing at all - the stdio buffer is lost with the process, so even
the console output stops several kilobytes short of the fault.

There is no debugger installed on this machine, so this walks the stack itself
with dbghelp, which ships with Windows. It reports every frame as
module+offset and does NOT resolve names: the first crash it caught in the
wild died inside SymFromAddr and left a header with an empty stack beneath it.
tools/xrprobe/symbolize.exe turns the offsets into function and line
afterwards, against the PDB, on a machine that is not currently crashing -
which also works when the crash happened on someone elses PC.

Deliberately not routed through common->Printf: by the time this runs the
engine may be in any state, and the point is to get the text out. It writes to
stderr and to crash.txt in the working directory, flushing each line.
================
*/

static FILE *crashLog = NULL;

static DWORD	pcvr_mainThreadId = 0;

// One slot per thread that ever reports a stage. Eight is more than the two
// that do - the main thread and the render backend - and a global single slot
// was the flaw in the last report: whichever thread wrote last erased where
// the other one was, which is exactly the half that mattered.
#define PCVR_MAX_STAGE_THREADS	8

struct pcvrStageSlot_t {
	DWORD			thread;
	const char		*what;
	int				ms;
};

static pcvrStageSlot_t	pcvr_stages[ PCVR_MAX_STAGE_THREADS ];

// The OpenXR frame calls, counted. A frame is wait, begin, end - so these
// either add up or they do not, and that is the whole question.
volatile long	pcvr_xrWaitStarted = 0;
volatile long	pcvr_xrWaitReturned = 0;
volatile long	pcvr_xrBegan = 0;
volatile long	pcvr_xrEnded = 0;

extern "C" void PCVR_CountXr( int which ) {
	switch ( which ) {
		case 0: InterlockedIncrement( &pcvr_xrWaitStarted ); break;
		case 1: InterlockedIncrement( &pcvr_xrWaitReturned ); break;
		case 2: InterlockedIncrement( &pcvr_xrBegan ); break;
		case 3: InterlockedIncrement( &pcvr_xrEnded ); break;
	}
}


// Doom3Quest_SurfaceView.c - how many times two threads both tried to start
// the same VR frame. Before the guard there, that was the pause-menu deadlock.
extern long			pcvr_layerSwitches;
extern const char	*pcvr_lastLayer;
extern int			pcvr_lastLayerFrame;
extern int			pcvr_eyeW;
extern int			pcvr_eyeH;
extern long			pcvr_rebuilds;
extern const char	*pcvr_lastRebuildWhere;
extern int			pcvr_lastRebuildFrame;
extern "C" int	PCVR_ScreenLayerFlags( void );
extern "C" long	pcvr_initBySetup;
extern "C" long	pcvr_initByFinish;
extern "C" long	pcvr_initByRescue;
extern "C" long	pcvr_initSkipped;

// Defined with PCVR_Stage further down; the freeze report reads them.
extern volatile const char	*pcvr_stage;
extern volatile DWORD		pcvr_stageThread;
extern volatile int			pcvr_stageMs;
extern volatile DWORD		pcvr_backendThreadId;

static void CrashPrintf( const char *fmt, ... ) {
	char	buf[ 2048 ];
	va_list	ap;

	va_start( ap, fmt );
	idStr::vsnPrintf( buf, sizeof( buf ) - 1, fmt, ap );
	va_end( ap );
	buf[ sizeof( buf ) - 1 ] = 0;

	fputs( buf, stderr );
	fflush( stderr );

	if ( crashLog ) {
		fputs( buf, crashLog );
		fflush( crashLog );
	}
}

static const char *ExceptionName( DWORD code ) {
	switch ( code ) {
		case EXCEPTION_ACCESS_VIOLATION:		return "ACCESS_VIOLATION";
		case EXCEPTION_ILLEGAL_INSTRUCTION:		return "ILLEGAL_INSTRUCTION";
		case EXCEPTION_INT_DIVIDE_BY_ZERO:		return "INT_DIVIDE_BY_ZERO";
		case EXCEPTION_STACK_OVERFLOW:			return "STACK_OVERFLOW";
		case EXCEPTION_PRIV_INSTRUCTION:		return "PRIV_INSTRUCTION";
		case EXCEPTION_IN_PAGE_ERROR:			return "IN_PAGE_ERROR";
		case EXCEPTION_DATATYPE_MISALIGNMENT:	return "DATATYPE_MISALIGNMENT";
		default:								return "unknown";
	}
}

// Names the module an address falls in, and its offset from that module's base.
// This is what identifies a fault at address 0 as "called through a null
// pointer" rather than "executing garbage", and it works with no symbols at all.
static void DescribeAddress( DWORD64 addr, char *out, size_t outSize ) {
	HMODULE			mod = NULL;
	char			path[ MAX_PATH ];
	const char		*leaf;

	out[ 0 ] = 0;

	if ( addr == 0 ) {
		idStr::snPrintf( out, (int)outSize - 1, "NULL - called through a null function pointer" );
		out[ outSize - 1 ] = 0;
		return;
	}

	if ( !GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							  (LPCSTR)(uintptr_t)addr, &mod ) || !mod ) {
		idStr::snPrintf( out, (int)outSize - 1, "no module - unmapped or dynamically generated code" );
		out[ outSize - 1 ] = 0;
		return;
	}

	if ( !GetModuleFileNameA( mod, path, sizeof( path ) ) ) {
		path[ 0 ] = 0;
	}

	leaf = strrchr( path, '\\' );
	leaf = leaf ? leaf + 1 : path;

	idStr::snPrintf( out, (int)outSize - 1, "%s+0x%llx", leaf, (unsigned long long)( addr - (DWORD64)(uintptr_t)mod ) );
	out[ outSize - 1 ] = 0;
}

// A frame, as module+offset only. Deliberately no dbghelp symbol lookup here:
// the first crash this handler caught on another machine died inside
// SymFromAddr and wrote a header with an empty stack under it. Offsets are
// enough - tools/xrprobe/symbolize.exe resolves them afterwards against the
// PDB, on a machine that is not in the middle of crashing.
static void PrintFrame( int n, DWORD64 addr ) {
	char where[ MAX_PATH + 64 ];

	DescribeAddress( addr, where, sizeof( where ) );
	CrashPrintf( "  %2d  0x%016llx  %s\n", n, (unsigned long long)addr, where );
}

static LONG WINAPI PCVR_ExceptionFilter( EXCEPTION_POINTERS *ep ) {
	static volatile LONG	entered = 0;
	CONTEXT					ctx;
	STACKFRAME64			frame;
	DWORD					code;
	DWORD64					at;
	DWORD64					frames[ 64 ];
	int						count = 0;
	int						n;

	// A fault inside the handler must not recurse into it.
	if ( InterlockedExchange( &entered, 1 ) != 0 ) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// stdout is block-buffered (see the setvbuf note in Doom3Quest_SurfaceView.c);
	// push whatever is still sitting in it out before this thread dies, or the
	// console log stops several kilobytes short of the fault.
	fflush( stdout );

	// Beside the executable, not in the working directory. A crash report that
	// lands wherever the game happened to be launched from is a crash report
	// nobody finds - and if fopen fails there is no file at all, which looks
	// identical to the handler never running.
	{
		static char path[ MAX_PATH ];
		char *slash;

		if ( GetModuleFileNameA( NULL, path, sizeof( path ) - 16 ) ) {
			slash = strrchr( path, '\\' );
			if ( slash ) {
				strcpy( slash + 1, "crash.txt" );
				crashLog = fopen( path, "a" );
			}
		}

		if ( !crashLog ) {
			// PCVR: "a" and not "w". A crash here is very often followed by a
			// second one during teardown, and the second one used to truncate the
			// first - so every report received showed ~idEvent at
			// DLL_PROCESS_DETACH and the actual fault was gone. Append, and let
			// the file hold both in order.
			crashLog = fopen( "crash.txt", "a" );
		}
	}

	code = ep->ExceptionRecord->ExceptionCode;
	at = (DWORD64)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;

	CrashPrintf( "\n================ PreyVR crashed ================\n" );
	CrashPrintf( "exception 0x%08lx  %s\n", (unsigned long)code, ExceptionName( code ) );

	{
		char where[ MAX_PATH + 64 ];
		DescribeAddress( at, where, sizeof( where ) );
		CrashPrintf( "at        0x%016llx  %s\n", (unsigned long long)at, where );
	}

	if ( code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 ) {
		CrashPrintf( "%s address 0x%016llx\n",
					 ep->ExceptionRecord->ExceptionInformation[ 0 ] ? "writing" : "reading",
					 (unsigned long long)ep->ExceptionRecord->ExceptionInformation[ 1 ] );
	}

	CrashPrintf( "thread    %lu\n", (unsigned long)GetCurrentThreadId() );

	// fInvadeProcess FALSE, and no symbol path. Loading symbols for every module
	// is what the previous version did, and it is both slow and a good way to
	// fault a second time. StackWalk64 only needs the module list, which
	// SymRefreshModuleList supplies - x64 unwinding comes out of each image's
	// own exception directory, not the PDB.
	SymSetOptions( SYMOPT_DEFERRED_LOADS );
	SymInitialize( GetCurrentProcess(), NULL, FALSE );
	SymRefreshModuleList( GetCurrentProcess() );

	ctx = *ep->ContextRecord;

	memset( &frame, 0, sizeof( frame ) );
	frame.AddrPC.Offset = ctx.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;

	frames[ count++ ] = frame.AddrPC.Offset;

	// A call through a null pointer leaves nothing for StackWalk64 to start
	// from, so take the return address the call pushed off the stack top.
	if ( frame.AddrPC.Offset == 0 ) {
		DWORD64 ret = 0;

		if ( ReadProcessMemory( GetCurrentProcess(), (LPCVOID)(uintptr_t)ctx.Rsp,
								&ret, sizeof( ret ), NULL ) && ret ) {
			frames[ count++ ] = ret;
			frame.AddrPC.Offset = ret;
			ctx.Rip = ret;
			ctx.Rsp += sizeof( ret );
			frame.AddrStack.Offset = ctx.Rsp;
		}
	}

	// Collect the whole walk before writing any of it. If StackWalk64 does fault,
	// the frames already gathered still get printed by the loop below.
	while ( count < (int)( sizeof( frames ) / sizeof( frames[ 0 ] ) ) ) {
		if ( !StackWalk64( IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(),
						   &frame, &ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL ) ) {
			break;
		}

		if ( frame.AddrPC.Offset == 0 ) {
			break;
		}

		frames[ count++ ] = frame.AddrPC.Offset;
	}

	CrashPrintf( "\nstack (%d frames):\n", count );

	for ( n = 0; n < count; n++ ) {
		PrintFrame( n, frames[ n ] );
	}

	CrashPrintf( "\nResolve these with:\n" );
	CrashPrintf( "  symbolize.exe PreyVR.exe <offset> [offset ...]\n" );
	CrashPrintf( "written to crash.txt\n" );
	CrashPrintf( "===============================================\n" );

	// PCVR: leave without running DLL_PROCESS_DETACH.
	//
	// Returning EXCEPTION_EXECUTE_HANDLER terminates the process the normal
	// way, which runs every DLL's detach handler - and this game module's
	// static destructors walk EventPool after eventDataAllocator has already
	// been destroyed, so ~idEvent faults on the way out. That second crash
	// re-entered this filter and, before the append above, overwrote the
	// report for the first one.
	//
	// It is the same reasoning as idCommonLocal::Quit: a process that is
	// leaving has nothing to preserve, and TerminateProcess runs no detach
	// handlers. Milestone 12 fixed the ~idEvent teardown for the quit path by
	// restoring Quit(); the crash path never went through it.
	fflush( NULL );
	TerminateProcess( GetCurrentProcess(), 0xC0000005 );
	CrashPrintf( "===============================================\n" );

	if ( crashLog ) {
		fclose( crashLog );
		crashLog = NULL;
	}

	return EXCEPTION_EXECUTE_HANDLER;
}

extern "C" void PCVR_InstallCrashHandler( void ) {
	SetUnhandledExceptionFilter( PCVR_ExceptionFilter );
}

/*
================
PCVR: the freeze watchdog

A crash throws, and the exception filter above catches it. A freeze throws
nothing at all: the process sits there, holding its window, until it is killed
- and then there is nothing to read but a log that ends mid-sentence.

So something outside the frame loop has to notice. This thread watches the
frame serial, and when it stops moving for pcvr_freezeSeconds it suspends
every other thread in the process and walks each one. A deadlock needs two
parties and this is the multithreaded d3es tree, where the frontend and the
render backend are separate threads, so a report naming only the main thread
would name at most half of it.

Three things about how it writes:

  * WriteFile, never stdio. The threads are suspended when this runs, and if
    the main thread happened to be inside printf it is holding the CRT's lock;
    a watchdog that then calls fprintf deadlocks against the very thing it was
    sent to describe.
  * dbghelp is initialised at startup, not here. SymInitialize while threads
    are frozen is a good way to turn a diagnosable hang into a hung diagnostic.
  * module+offset, no symbol lookup - the same reasoning as the crash handler,
    and symbolize.exe resolves it afterwards on a machine that is not stuck.

It never kills the process. A long level load looks exactly like a freeze from
out here, and the honest thing is to say how long there has been no frame and
let whoever reads it decide.
================
*/

volatile LONG		pcvr_frameSerial = 0;

idCVar pcvr_freezeSeconds( "pcvr_freezeSeconds", "15", CVAR_SYSTEM | CVAR_INTEGER | CVAR_ARCHIVE,
		"PCVR: write freeze.txt when no frame completes for this many seconds (0 = off)", 0, 600 );
idCVar pcvr_freezeRepeat( "pcvr_freezeRepeat", "30", CVAR_SYSTEM | CVAR_INTEGER,
		"PCVR: seconds between repeat freeze reports while it stays stuck", 5, 600 );
idCVar pcvr_freezeTest( "pcvr_freezeTest", "0", CVAR_SYSTEM | CVAR_INTEGER,
		"PCVR: stall the frame loop this many seconds once, to prove the watchdog fires", 0, 120 );

static HANDLE	pcvr_freezeFile = INVALID_HANDLE_VALUE;

static void FreezeWrite( const char *fmt, ... ) {
	char	buf[ 2048 ];
	va_list	ap;
	DWORD	wrote;

	va_start( ap, fmt );
	idStr::vsnPrintf( buf, sizeof( buf ) - 1, fmt, ap );
	va_end( ap );
	buf[ sizeof( buf ) - 1 ] = 0;

	if ( pcvr_freezeFile != INVALID_HANDLE_VALUE ) {
		WriteFile( pcvr_freezeFile, buf, (DWORD)strlen( buf ), &wrote, NULL );
	}

	// GetStdHandle rather than stderr, for the CRT-lock reason above.
	{
		HANDLE err = GetStdHandle( STD_ERROR_HANDLE );

		if ( err && err != INVALID_HANDLE_VALUE ) {
			WriteFile( err, buf, (DWORD)strlen( buf ), &wrote, NULL );
		}
	}
}

// Same walk as the crash handler, against a thread that is not this one and is
// currently suspended, so the context comes from GetThreadContext.
static void FreezeWalkThread( DWORD tid ) {
	HANDLE			th;
	CONTEXT			ctx;
	STACKFRAME64	frame;
	DWORD64			frames[ 48 ];
	int				count = 0;
	int				n;

	th = OpenThread( THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
					 FALSE, tid );
	if ( !th ) {
		FreezeWrite( "  thread %lu: cannot open\n", (unsigned long)tid );
		return;
	}

	if ( SuspendThread( th ) == (DWORD)-1 ) {
		FreezeWrite( "  thread %lu: cannot suspend\n", (unsigned long)tid );
		CloseHandle( th );
		return;
	}

	memset( &ctx, 0, sizeof( ctx ) );
	ctx.ContextFlags = CONTEXT_FULL;

	if ( !GetThreadContext( th, &ctx ) ) {
		FreezeWrite( "  thread %lu: no context\n", (unsigned long)tid );
		ResumeThread( th );
		CloseHandle( th );
		return;
	}

	memset( &frame, 0, sizeof( frame ) );
	frame.AddrPC.Offset = ctx.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;

	frames[ count++ ] = frame.AddrPC.Offset;

	while ( count < (int)( sizeof( frames ) / sizeof( frames[ 0 ] ) ) ) {
		if ( !StackWalk64( IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), th,
						   &frame, &ctx, NULL, SymFunctionTableAccess64,
						   SymGetModuleBase64, NULL ) ) {
			break;
		}
		if ( frame.AddrPC.Offset == 0 ) {
			break;
		}
		frames[ count++ ] = frame.AddrPC.Offset;
	}

	// Resumed before anything is written: the write can block, and a thread
	// left suspended while this one blocks is how a diagnostic becomes the bug.
	ResumeThread( th );

	{
		const char *label = "";

		if ( tid == pcvr_mainThreadId ) {
			label = "  <- the frame loop";
		} else if ( tid == pcvr_backendThreadId ) {
			label = "  <- THE RENDER BACKEND";
		}
		FreezeWrite( "\n  thread %lu (%d frames)%s\n",
					 (unsigned long)tid, count, label );
	}

	for ( n = 0; n < count; n++ ) {
		char where[ MAX_PATH + 64 ];

		DescribeAddress( frames[ n ], where, sizeof( where ) );
		FreezeWrite( "    %2d  0x%016llx  %s\n", n,
					 (unsigned long long)frames[ n ], where );
	}

	CloseHandle( th );
}

static void FreezeReport( int stalledMs, int serial ) {
	HANDLE			snap;
	THREADENTRY32	te;
	SYSTEMTIME		st;
	char			path[ MAX_PATH ];
	DWORD			me = GetCurrentThreadId();
	DWORD			pid = GetCurrentProcessId();

	// Beside the executable, for the same reason as crash.txt: a report that
	// lands wherever the game was launched from is a report nobody finds.
	if ( GetModuleFileNameA( NULL, path, sizeof( path ) ) ) {
		char *slash = strrchr( path, '\\' );

		if ( slash ) {
			slash[ 1 ] = 0;
			idStr::Append( path, sizeof( path ), "freeze.txt" );
		} else {
			idStr::Copynz( path, "freeze.txt", sizeof( path ) );
		}
	} else {
		idStr::Copynz( path, "freeze.txt", sizeof( path ) );
	}

	if ( pcvr_freezeFile == INVALID_HANDLE_VALUE ) {
		pcvr_freezeFile = CreateFileA( path, FILE_APPEND_DATA, FILE_SHARE_READ,
									   NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
	}

	GetLocalTime( &st );
	FreezeWrite( "\n================ PreyVR is not drawing ================\n" );
	FreezeWrite( "%04d-%02d-%02d %02d:%02d:%02d\n",
				 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
	FreezeWrite( "no frame for %d.%01d seconds, at frame %d\n",
				 stalledMs / 1000, ( stalledMs % 1000 ) / 100, serial );
	FreezeWrite( "if the game was loading a level, that is what this is.\n" );

	// The breadcrumb. This is the line that says which call is stuck when the
	// stack below stops at a driver frame and cannot get back to our code.
	FreezeWrite( "last stage    %s\n", pcvr_stage ? pcvr_stage : "(none)" );
	FreezeWrite( "  set by thread %lu, %d ms ago\n",
				 (unsigned long)pcvr_stageThread,
				 Sys_Milliseconds() - pcvr_stageMs );
	FreezeWrite( "render backend is thread %lu\n",
				 (unsigned long)pcvr_backendThreadId );
	// Which of the five flags useScreenLayer() is made of was set. That says
	// which transition the renderer was in the middle of - a menu, a cinematic,
	// a level load - rather than leaving it to be guessed from what was happening.
	{
		const int f = PCVR_ScreenLayerFlags();

		FreezeWrite( "screen layer  %s%s%s%s%s%s\n",
					 f ? "" : "off (in the world)",
					 ( f & 1 ) ? "inMenu " : "",
					 ( f & 2 ) ? "forceVirtualScreen " : "",
					 ( f & 4 ) ? "inCinematic " : "",
					 ( f & 8 ) ? "loading " : "",
					 ( f & 16 ) ? "consoleShown " : "" );
	}

	FreezeWrite( "eye buffer    %d x %d\n", pcvr_eyeW, pcvr_eyeH );
	FreezeWrite( "layer switches: %ld, last to %s at frame %d\n",
				 pcvr_layerSwitches, pcvr_lastLayer, pcvr_lastLayerFrame );
	FreezeWrite( "pose agreement (0 = layer matches the picture): "
				 "0:%ld 1:%ld 2:%ld 3:%ld more:%ld\n",
				 pcvr_poseLagHist[ 0 ], pcvr_poseLagHist[ 1 ],
				 pcvr_poseLagHist[ 2 ], pcvr_poseLagHist[ 3 ],
				 pcvr_poseLagWorse );
	FreezeWrite( "renderer rebuilds: %ld, last at frame %d - %s\n",
				 pcvr_rebuilds, pcvr_lastRebuildFrame, pcvr_lastRebuildWhere );

	FreezeWrite( "\nwho started each VR frame:\n" );
	FreezeWrite( "  from FrameSetup (main)      %ld\n", pcvr_initBySetup );
	FreezeWrite( "  from finishEyeBuffer (back) %ld\n", pcvr_initByFinish );
	FreezeWrite( "  rescued in prepareEyeBuffer %ld\n", pcvr_initByRescue );
	FreezeWrite( "  already started, skipped    %ld\n", pcvr_initSkipped );

	// A frame is wait, begin, end. Started minus returned is how many threads
	// are sitting inside xrWaitFrame right now: one is the frame in flight,
	// two would mean the pairing is ours. begins against ends says whether a
	// frame was left open.
	FreezeWrite( "\nOpenXR frame calls:\n" );
	FreezeWrite( "  xrWaitFrame  started %ld  returned %ld  (inside now: %ld)\n",
				 pcvr_xrWaitStarted, pcvr_xrWaitReturned,
				 pcvr_xrWaitStarted - pcvr_xrWaitReturned );
	FreezeWrite( "  xrBeginFrame %ld\n", pcvr_xrBegan );
	FreezeWrite( "  xrEndFrame   %ld\n", pcvr_xrEnded );
	FreezeWrite( "  begun but not ended: %ld\n", pcvr_xrBegan - pcvr_xrEnded );

	FreezeWrite( "\nwhere each thread last was:\n" );
	{
		int i;

		for ( i = 0; i < PCVR_MAX_STAGE_THREADS; i++ ) {
			if ( pcvr_stages[ i ].thread == 0 ) {
				continue;
			}
			FreezeWrite( "  thread %-6lu %6d ms ago  %s%s\n",
						 (unsigned long)pcvr_stages[ i ].thread,
						 Sys_Milliseconds() - pcvr_stages[ i ].ms,
						 pcvr_stages[ i ].what ? pcvr_stages[ i ].what : "(none)",
						 pcvr_stages[ i ].thread == pcvr_backendThreadId
							? "   <- the backend" : "" );
		}
	}

	snap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
	if ( snap == INVALID_HANDLE_VALUE ) {
		FreezeWrite( "no thread list\n" );
		return;
	}

	memset( &te, 0, sizeof( te ) );
	te.dwSize = sizeof( te );

	if ( Thread32First( snap, &te ) ) {
		do {
			if ( te.th32OwnerProcessID != pid ) {
				continue;
			}
			if ( te.th32ThreadID == me ) {
				continue;	// the watchdog itself
			}
			FreezeWalkThread( te.th32ThreadID );
		} while ( Thread32Next( snap, &te ) );
	}

	CloseHandle( snap );

	FreezeWrite( "\nResolve these with:\n" );
	FreezeWrite( "  symbolize.exe PreyVR.exe <offset> [offset ...]\n" );
	FreezeWrite( "======================================================\n" );
}

static DWORD WINAPI PCVR_WatchdogProc( LPVOID ) {
	int		lastSerial = 0;
	DWORD	lastChange = GetTickCount();
	DWORD	lastReport = 0;
	bool	started = false;

	for ( ;; ) {
		int		serial;
		DWORD	now;
		int		limit;

		Sleep( 1000 );

		serial = (int)InterlockedCompareExchange( &pcvr_frameSerial, 0, 0 );
		now = GetTickCount();

		// Nothing is watched until the engine has drawn once. Before that the
		// cvar system may not be up, and every startup would look like a hang.
		if ( !started ) {
			if ( serial == 0 ) {
				continue;
			}
			started = true;
			lastSerial = serial;
			lastChange = now;
			continue;
		}

		if ( serial != lastSerial ) {
			lastSerial = serial;
			lastChange = now;
			lastReport = 0;
			continue;
		}

		limit = pcvr_freezeSeconds.GetInteger();
		if ( limit <= 0 ) {
			continue;
		}

		if ( (int)( now - lastChange ) < limit * 1000 ) {
			continue;
		}
		if ( lastReport != 0 &&
			 (int)( now - lastReport ) < pcvr_freezeRepeat.GetInteger() * 1000 ) {
			continue;
		}

		lastReport = now;
		FreezeReport( (int)( now - lastChange ), serial );
	}
}

/*
================
PCVR_LogSystemInfo

Written at the top of qconsole.log the moment the file opens. Every line here
is something that would otherwise have to be asked for, one message at a time,
before a report could be acted on at all.

The GPU, its driver and the OpenXR runtime are not repeated here: the renderer
prints GL_VENDOR/GL_RENDERER/GL_VERSION when it starts, and the VR layer names
the runtime and its version. Both land in this same file a few hundred lines
below.
================
*/

/*
================
PCVR_Stage

Where the renderer is, as a string, for the freeze report.

A stack walk cannot get out of nvoglv64.dll or virtualdesktop-openxr.dll and
back into our frames, so a thread blocked inside either of them reports four
frames of driver and nothing that says which of our calls it is inside. This
costs one pointer store at each boundary and answers exactly that.

Not a lock, not a copy: a pointer to a string literal, which is atomic enough
for something read only after everything has already stopped.
================
*/
volatile const char	*pcvr_stage = "not started";
volatile DWORD		pcvr_stageThread = 0;
volatile int		pcvr_stageMs = 0;
volatile DWORD		pcvr_backendThreadId = 0;

// Every renderer rebuild, with the frame it happened on. Destroying the
// swapchain inside a frame is what wedged the runtime, so a report has to say
// how many rebuilds there were and which site did them.
long			pcvr_rebuilds = 0;
const char		*pcvr_lastRebuildWhere = "(none)";
int				pcvr_lastRebuildFrame = -1;

int				pcvr_eyeW = 0;
int				pcvr_eyeH = 0;

extern "C" void PCVR_NoteEyeSize( int w, int h ) {
	pcvr_eyeW = w;
	pcvr_eyeH = h;
}

long			pcvr_layerSwitches = 0;
const char		*pcvr_lastLayer = "(none yet)";
int				pcvr_lastLayerFrame = -1;

/*
================
PCVR: does the picture agree with the pose the compositor is told about?

He reports a small judder on head movement. The suspicion is that the
projection layer declares a newer pose than the one the world was drawn from,
so the compositor reprojects against a pose the image never had - which is not
latency, and feels exactly like a judder.

It cannot be measured as an angle on this desk: the null driver's head never
moves, so every pose is identical whatever the bug. What can be measured is
WHICH locate each side used. Every xrLocateViews takes a number; the game
records the number when it reads the orientation for its tic, and xrBeginFrame
records the number of the pose it hands to the layer. Equal numbers mean the
picture and the layer agree.

A histogram, not a mean: "0.4 frames behind on average" would hide most frames
being right and some being wrong, which is what an intermittent judder is.
================
*/
long			pcvr_locateGen = 0;
static long		pcvr_gameGen = 0;
long			pcvr_poseLagHist[ 4 ];
long			pcvr_poseLagWorse = 0;

extern "C" void PCVR_NoteLocate( void ) {
	InterlockedIncrement( &pcvr_locateGen );
}

extern "C" void PCVR_NoteGamePoseGen( void ) {
	pcvr_gameGen = pcvr_locateGen;
}

extern "C" void PCVR_NotePoseGen( long gen ) {
	const long lag = gen - pcvr_gameGen;

	if ( lag >= 0 && lag < 4 ) {
		pcvr_poseLagHist[ lag ]++;
	} else {
		pcvr_poseLagWorse++;
	}
}

// PCVR: the haptic call, timed, because it is made from the game thread.
//
// Every OpenXR frame call in this port had to be moved onto the thread that
// holds the GL context; xrApplyHapticFeedback is the one session call still
// made from the main thread, and it fires on every shot. If a runtime takes a
// lock the frame loop also wants, this is where the main thread pays for it.
//
// The null driver has no controllers, so nothing on the desk can fire it.
// Shipped as a measurement rather than guessed at.
static int	pcvr_hapticCalls = 0;
static int	pcvr_hapticWorstUs = 0;
static double	pcvr_hapticSumUs = 0;
static int	pcvr_hapticOver1ms = 0;

extern "C" void PCVR_NoteHaptic( int us ) {
	pcvr_hapticCalls++;
	pcvr_hapticSumUs += us;
	if ( us > pcvr_hapticWorstUs ) {
		pcvr_hapticWorstUs = us;
	}
	if ( us >= 1000 ) {
		pcvr_hapticOver1ms++;
	}
}

/*
================
PCVR: how much of the frame period do we actually have left?

xrWaitFrame returns when the compositor is ready for the next frame, and the
frame is due one display period later - 11.1 ms at 90 Hz. Everything between
that return and xrEndFrame is ours to spend, and whatever is left over is the
slack that absorbs a hitch.

This matters because frame TIME can look perfect while the frame is still
missing its slot. His log has a median of 11 ms and 0.38% late, and Virtual
Desktop still reported 83-86 fps against a 90 target - so the frames were
produced on time and did not all arrive. Frame time cannot tell those apart.
Slack can: at 2 ms a 3 ms hiccup drops a frame, at 8 ms it does not.

Two spans, because they separate the two suspects:

  wait-return -> submit   everything we do with the frame
  begin       -> submit   just the drawing

If the first is near a full period while the second is short, the cost is
before the drawing - the backend picking the frame up - and that is the
pipeline arrangement rather than the renderer.

Microseconds, because at this scale milliseconds round the answer away.
================
*/
static LARGE_INTEGER	pcvr_qpcFreq;
static LARGE_INTEGER	pcvr_tWaitReturn;
static LARGE_INTEGER	pcvr_tBegin;

#define PCVR_SLACK_BUCKETS 40
static int	pcvr_waitToSubmit[ PCVR_SLACK_BUCKETS ];	// 0.5 ms buckets
static int	pcvr_beginToSubmit[ PCVR_SLACK_BUCKETS ];
static int	pcvr_slackCount = 0;
static double	pcvr_waitToSubmitSum = 0;
static double	pcvr_beginToSubmitSum = 0;
static int	pcvr_waitToSubmitWorst = 0;
static int	pcvr_overPeriod = 0;

static void PCVR_Bucket( int *hist, int us ) {
	int b = us / 500;
	if ( b < 0 ) {
		b = 0;
	}
	if ( b >= PCVR_SLACK_BUCKETS ) {
		b = PCVR_SLACK_BUCKETS - 1;
	}
	hist[ b ]++;
}

extern "C" void PCVR_NoteWaitReturn( void ) {
	if ( pcvr_qpcFreq.QuadPart == 0 ) {
		QueryPerformanceFrequency( &pcvr_qpcFreq );
	}
	QueryPerformanceCounter( &pcvr_tWaitReturn );
}

extern "C" void PCVR_NoteBeginFrameTime( void ) {
	QueryPerformanceCounter( &pcvr_tBegin );
}

extern "C" void PCVR_NoteSubmit( void ) {
	LARGE_INTEGER now;

	if ( pcvr_qpcFreq.QuadPart == 0 || pcvr_tWaitReturn.QuadPart == 0 ) {
		return;
	}
	QueryPerformanceCounter( &now );

	const int waitUs = (int)( ( ( now.QuadPart - pcvr_tWaitReturn.QuadPart ) * 1000000 )
			/ pcvr_qpcFreq.QuadPart );
	PCVR_Bucket( pcvr_waitToSubmit, waitUs );
	pcvr_waitToSubmitSum += waitUs;
	if ( waitUs > pcvr_waitToSubmitWorst ) {
		pcvr_waitToSubmitWorst = waitUs;
	}

	if ( pcvr_tBegin.QuadPart != 0 ) {
		const int beginUs = (int)( ( ( now.QuadPart - pcvr_tBegin.QuadPart ) * 1000000 )
				/ pcvr_qpcFreq.QuadPart );
		PCVR_Bucket( pcvr_beginToSubmit, beginUs );
		pcvr_beginToSubmitSum += beginUs;
	}
	pcvr_slackCount++;

	// A frame that used the whole period had no slack at all: it became due
	// at the moment it was handed over, and anything at all in the way of it
	// missed the slot. This is the number frame time cannot show.
	{
		const int refresh = renderSystem ? renderSystem->GetRefresh() : 90;
		const int periodUs = refresh > 0 ? ( 1000000 / refresh ) : 11111;

		if ( waitUs > periodUs ) {
			pcvr_overPeriod++;
		}
	}
}

static int PCVR_Pct( const int *hist, int pct ) {
	int total = 0;
	for ( int i = 0; i < PCVR_SLACK_BUCKETS; i++ ) {
		total += hist[ i ];
	}
	if ( total < 1 ) {
		return 0;
	}
	int run = 0;
	for ( int i = 0; i < PCVR_SLACK_BUCKETS; i++ ) {
		run += hist[ i ];
		if ( run * 100 >= total * pct ) {
			return i * 500;
		}
	}
	return ( PCVR_SLACK_BUCKETS - 1 ) * 500;
}


/*
================
PCVR: how long the OpenXR calls themselves take.

Every measurement so far stops at the moment we CALL xrEndFrame. What the
runtime then does with the frame has never been timed, and for this port that
is not a small gap: PreyVR is an OpenGL application, and VirtualDesktopXR is a
Direct3D runtime. Our eye texture has to cross that boundary somewhere, and
the only places it can be crossing it are these five calls.

It is also the one thing that is different about us. His other headset titles
are native D3D and are, in his words, rock solid on the same link - so a cost
that only an OpenGL client pays would show up here and nowhere else.

Microseconds. If these are all a few hundred, the runtime is not the problem
and I have to look elsewhere again.
================
*/
#define PCVR_XR_CALLS 5
static const char *pcvr_xrCallName[ PCVR_XR_CALLS ] = {
	"xrBeginFrame", "xrAcquireSwapchainImage", "xrWaitSwapchainImage",
	"xrReleaseSwapchainImage", "xrEndFrame"
};
static LARGE_INTEGER	pcvr_xrIn;
static double	pcvr_xrSum[ PCVR_XR_CALLS ];
static int	pcvr_xrCount[ PCVR_XR_CALLS ];
static int	pcvr_xrWorst[ PCVR_XR_CALLS ];
static int	pcvr_xrOver2ms[ PCVR_XR_CALLS ];

// PCVR: the VR layer logs with ALOGE, which is printf on Windows - stdout,
// which nobody captures. The game is launched from a bat with no console window and
// mails me qconsole.log, so anything written that way does not exist. The
// swapchain format list was, briefly, written that way.
extern "C" void PCVR_Log( const char *line ) {
	// Same rule: ovrFramebuffer_Create runs on the backend during a renderer
	// rebuild, so this queues too rather than printing where it stands.
	if ( line ) {
		PCVR_Defer( line );
	}
}


// PCVR: how many portal subviews a frame actually draws.
//
// pcvr_portalDistance defaults to 0, which ignores the draw distance the map
// authored and renders every portal at any range. That is a deliberate choice and a
// defensible one - the budget it protects was set for 2006 hardware - but each
// portal in range is a FULL EXTRA SCENE RENDER, so the cost is worth knowing
// rather than assuming.
//
// "beyond the map limit" counts the ones retail would have skipped: that is
// exactly the work this default adds, and nothing else in the report can show
// it. If the frame rate ever sags in a portal-heavy room, this is the number
// to read first.
static int	pcvr_portalSubviews = 0;
static int	pcvr_portalBeyondLimit = 0;
static int	pcvr_portalThisFrame = 0;
static int	pcvr_portalWorstFrame = 0;

extern "C" void PCVR_NotePortalSubview( int beyondMapLimit ) {
	pcvr_portalSubviews++;
	pcvr_portalThisFrame++;
	if ( beyondMapLimit ) {
		pcvr_portalBeyondLimit++;
	}
	if ( pcvr_portalThisFrame > pcvr_portalWorstFrame ) {
		pcvr_portalWorstFrame = pcvr_portalThisFrame;
	}
}

// PCVR: what the desktop mirror costs.
//
// A mirror in a VR port is the classic way to pin a 90 Hz session to the
// monitor's refresh - 15 ms a frame on CSVR while the game rendered in 1.6,
// and it took five wrong guesses to find because nothing measured it. This
// measures it. If the p50 is a fraction of a millisecond the mirror is free;
// if it sits near a display period, the swap is blocking and the driver is
// ignoring swap interval 0.
static int	pcvr_mirrorCount = 0;
static double	pcvr_mirrorSum = 0;
static int	pcvr_mirrorWorstUs = 0;
static int	pcvr_mirrorOver2ms = 0;
static LARGE_INTEGER	pcvr_mirrorT0;

extern "C" void PCVR_MirrorBegin( void ) {
	if ( pcvr_qpcFreq.QuadPart == 0 ) {
		QueryPerformanceFrequency( &pcvr_qpcFreq );
	}
	QueryPerformanceCounter( &pcvr_mirrorT0 );
}

extern "C" void PCVR_MirrorEnd( void ) {
	LARGE_INTEGER now;

	if ( pcvr_qpcFreq.QuadPart == 0 || pcvr_mirrorT0.QuadPart == 0 ) {
		return;
	}
	QueryPerformanceCounter( &now );
	{
		const int us = (int)( ( ( now.QuadPart - pcvr_mirrorT0.QuadPart ) * 1000000 )
				/ pcvr_qpcFreq.QuadPart );

		pcvr_mirrorCount++;
		pcvr_mirrorSum += us;
		if ( us > pcvr_mirrorWorstUs ) {
			pcvr_mirrorWorstUs = us;
		}
		if ( us >= 2000 ) {
			pcvr_mirrorOver2ms++;
		}
	}
}

void PCVR_ReportMirror( void ) {
	if ( pcvr_mirrorCount < 1 ) {
		return;					// silent when the mirror was never on
	}
	double blitUs = 0.0, swapUs = 0.0;
	int timed = 0, asked = -2, ret = 0, readBack = -2;

	PCVR_MirrorStats( &blitUs, &swapUs, &timed, &asked, &ret, &readBack );

	common->Printf( "PCVR desktop mirror: %d presented, %d skipped by the pacer  "
			"mean %.0f us  worst %.1f ms  over 2 ms: %d\n",
			pcvr_mirrorCount, PCVR_MirrorSkips(),
			pcvr_mirrorSum / (double)pcvr_mirrorCount,
			pcvr_mirrorWorstUs / 1000.0,
			pcvr_mirrorOver2ms );

	// Which half holds the frame. The blit figure includes the bind of the
	// default framebuffer, which is where a full swap queue makes us wait.
	if ( timed > 0 ) {
		common->Printf( "PCVR desktop mirror: blit %.0f us, swap %.0f us per "
				"presented frame\n", blitUs, swapUs );
	}

	// Whether the swap interval request did anything - which nothing has ever
	// been able to say, because ALOGE is printf and this is a GUI app.
	if ( asked == -1 ) {
		common->Printf( "PCVR desktop mirror: no wglSwapIntervalEXT - vsync locked\n" );
	} else if ( asked >= 0 ) {
		common->Printf( "PCVR desktop mirror: swap interval asked %d, call %s, "
				"driver reads back %s\n", asked, ret ? "succeeded" : "FAILED",
				readBack == -1 ? "(no wglGetSwapIntervalEXT)" :
				readBack == 0 ? "0 (off)" : readBack == 1 ? "1 (ON - vsync)" : "other" );
	}
}

extern "C" void PCVR_ResetPortalFrame( void ) {
	pcvr_portalThisFrame = 0;
}

void PCVR_ReportPortals( int frames ) {
	if ( pcvr_portalSubviews < 1 ) {
		common->Printf( "PCVR portal subviews: none drawn this run\n" );
		return;
	}
	common->Printf( "PCVR portal subviews: %d total, %.2f per frame, worst frame %d\n",
			pcvr_portalSubviews,
			frames > 0 ? ( (double)pcvr_portalSubviews / (double)frames ) : 0.0,
			pcvr_portalWorstFrame );
	common->Printf( "  of those, %d (%.1f%%) are beyond the distance the map authored - the extra work pcvr_portalDistance 0 buys\n",
			pcvr_portalBeyondLimit,
			( 100.0 * pcvr_portalBeyondLimit ) / (double)pcvr_portalSubviews );
}
extern "C" void PCVR_XrIn( void ) {
	if ( pcvr_qpcFreq.QuadPart == 0 ) {
		QueryPerformanceFrequency( &pcvr_qpcFreq );
	}
	QueryPerformanceCounter( &pcvr_xrIn );
}

extern "C" void PCVR_XrOut( int which ) {
	LARGE_INTEGER now;

	if ( which < 0 || which >= PCVR_XR_CALLS || pcvr_qpcFreq.QuadPart == 0 ) {
		return;
	}
	QueryPerformanceCounter( &now );

	const int us = (int)( ( ( now.QuadPart - pcvr_xrIn.QuadPart ) * 1000000 )
			/ pcvr_qpcFreq.QuadPart );

	pcvr_xrSum[ which ] += us;
	pcvr_xrCount[ which ]++;
	if ( us > pcvr_xrWorst[ which ] ) {
		pcvr_xrWorst[ which ] = us;
	}
	if ( us >= 2000 ) {
		pcvr_xrOver2ms[ which ]++;
	}
}

void PCVR_ReportXrCalls( void ) {
	common->Printf( "PCVR OpenXR call cost (this is an OpenGL client on a D3D runtime):\n" );
	for ( int i = 0; i < PCVR_XR_CALLS; i++ ) {
		if ( pcvr_xrCount[ i ] < 1 ) {
			continue;
		}
		common->Printf( "  %-24s mean %6.0f us  worst %7.1f ms  over 2 ms: %d of %d\n",
				pcvr_xrCallName[ i ],
				pcvr_xrSum[ i ] / (double)pcvr_xrCount[ i ],
				pcvr_xrWorst[ i ] / 1000.0,
				pcvr_xrOver2ms[ i ], pcvr_xrCount[ i ] );
	}
}
void PCVR_ReportSlack( void ) {
	if ( pcvr_slackCount < 1 ) {
		common->Printf( "PCVR frame budget: no frames submitted\n" );
		return;
	}

	const int refresh = renderSystem ? renderSystem->GetRefresh() : 90;
	const double periodUs = refresh > 0 ? ( 1000000.0 / refresh ) : 11111.0;
	const double meanWait = pcvr_waitToSubmitSum / (double)pcvr_slackCount;

	common->Printf( "PCVR frame budget (period %.2f ms at %d Hz):\n", periodUs / 1000.0, refresh );
	// Percentiles, not the mean. A level load sits between a wait and a
	// submit for nine seconds, and one of those drags the mean from 1 ms to
	// 4.3 - which read as "we use 40% of the budget" when the true figure
	// is 9%. The worst case is still printed, and is nearly always a load.
	const int p50 = PCVR_Pct( pcvr_waitToSubmit, 50 );
	const int p99 = PCVR_Pct( pcvr_waitToSubmit, 99 );

	common->Printf( "  wait-return to submit: p50 %.1f ms  p95 %.1f  p99 %.1f  (mean %.2f, worst %.0f - both include level loads)\n",
			p50 / 1000.0,
			PCVR_Pct( pcvr_waitToSubmit, 95 ) / 1000.0,
			p99 / 1000.0,
			meanWait / 1000.0,
			pcvr_waitToSubmitWorst / 1000.0 );
	common->Printf( "  of which drawing:      mean %.2f ms  p95 %.1f ms\n",
			( pcvr_beginToSubmitSum / (double)pcvr_slackCount ) / 1000.0,
			PCVR_Pct( pcvr_beginToSubmit, 95 ) / 1000.0 );
	common->Printf( "  SLACK LEFT:            %.1f ms typical, %.1f ms at p99  (negative means the frame was already due)\n",
			( periodUs - p50 ) / 1000.0, ( periodUs - p99 ) / 1000.0 );
	common->Printf( "  used the WHOLE period (no slack left): %d of %d (%.2f%%)\n",
			pcvr_overPeriod, pcvr_slackCount,
			( 100.0 * pcvr_overPeriod ) / (double)pcvr_slackCount );

	common->Printf( "PCVR wait-to-submit histogram (0.5 ms buckets, ms: count):" );
	for ( int i = 0; i < PCVR_SLACK_BUCKETS; i++ ) {
		if ( pcvr_waitToSubmit[ i ] > 0 ) {
			common->Printf( " %.1f:%d", i * 0.5, pcvr_waitToSubmit[ i ] );
		}
	}
	common->Printf( "\n" );
}

void PCVR_ReportHaptics( void ) {
	if ( pcvr_hapticCalls < 1 ) {
		common->Printf( "PCVR haptics: no calls this run\n" );
		return;
	}
	common->Printf( "PCVR haptics on the main thread: %d calls  mean %.0f us  worst %d us  over 1 ms: %d\n",
			pcvr_hapticCalls,
			pcvr_hapticSumUs / (double)pcvr_hapticCalls,
			pcvr_hapticWorstUs, pcvr_hapticOver1ms );
}

/*
================
PCVR: how stale is the picture, and does the staleness hold still?

Pose agreement above answers "did the layer declare the pose the world was
drawn from", and since VR_BeginFrame now copies that pose deliberately the
answer is yes, always. It is very nearly a tautology, and it never explained
the judder.

This is the question it should have been asking. The submitted frame says it
is for display time T, and the pose it was drawn from was predicted for an
earlier time G. T - G is how far behind the picture is, in milliseconds.

The distinction that matters:

  a CONSTANT age is LATENCY. The compositor reprojects it away and the head
  feels attached to the world, just slightly behind it. Nobody reports
  latency as judder.

  a VARYING age is JUDDER. The world lurches by the difference, once per
  frame, in whatever direction the head happens to be turning - which is
  exactly "a small tiny judder whenever I moved around".

So the number to read is the SPREAD, not the mean. A run that is 11 11 11 11
is fine. A run that is 0 11 0 22 11 averages the same and feels terrible.

The submit interval beside it is the control: if the compositor is handing us
an even cadence and our age still jumps, the unevenness is ours.

Both work on the null driver with nobody in the room - they are times, not
angles, and the synthetic HMD keeps real ones.
================
*/
#define PCVR_AGE_MAX 48
static int	pcvr_ageHist[ PCVR_AGE_MAX ];
static int	pcvr_ageNegative = 0;
static int	pcvr_ageOver = 0;
static int	pcvr_ageMin = 1 << 30;
static int	pcvr_ageMax = -( 1 << 30 );
static double	pcvr_ageSum = 0;
static double	pcvr_ageSumSq = 0;
static int	pcvr_ageCount = 0;
static int	pcvr_ageLast = -1;
static int	pcvr_ageChanges = 0;

extern "C" void PCVR_NotePoseAge( int ms ) {
	if ( ms < 0 ) {
		pcvr_ageNegative++;
	} else if ( ms >= PCVR_AGE_MAX ) {
		pcvr_ageOver++;
	} else {
		pcvr_ageHist[ ms ]++;
	}

	if ( ms < pcvr_ageMin ) {
		pcvr_ageMin = ms;
	}
	if ( ms > pcvr_ageMax ) {
		pcvr_ageMax = ms;
	}
	pcvr_ageSum += ms;
	pcvr_ageSumSq += (double)ms * (double)ms;
	pcvr_ageCount++;

	// How often the staleness CHANGED from one frame to the next. This is the
	// judder number: every change is one visible lurch.
	if ( pcvr_ageLast >= 0 && ms != pcvr_ageLast ) {
		pcvr_ageChanges++;
	}
	pcvr_ageLast = ms;
}

#define PCVR_IVL_MAX 48
static int	pcvr_ivlHist[ PCVR_IVL_MAX ];
static int	pcvr_ivlOver = 0;
static int	pcvr_ivlCount = 0;

extern "C" void PCVR_NoteSubmitInterval( int ms ) {
	if ( ms < 0 ) {
		ms = 0;
	}
	if ( ms >= PCVR_IVL_MAX ) {
		pcvr_ivlOver++;
	} else {
		pcvr_ivlHist[ ms ]++;
	}
	pcvr_ivlCount++;
}

void PCVR_ReportPoseAge( void ) {
	if ( pcvr_ageCount < 1 ) {
		common->Printf( "PCVR pose age: no frames submitted\n" );
		return;
	}

	const double mean = pcvr_ageSum / (double)pcvr_ageCount;
	const double var = ( pcvr_ageSumSq / (double)pcvr_ageCount ) - ( mean * mean );
	const double sd = var > 0 ? sqrt( var ) : 0.0;

	common->Printf( "PCVR pose age (picture staleness at submit): "
			"mean %.2f ms  sd %.2f ms  min %d  max %d  over %d  negative %d\n",
			mean, sd, pcvr_ageMin, pcvr_ageMax, pcvr_ageOver, pcvr_ageNegative );
	common->Printf( "PCVR pose age changed between frames: %d of %d (%.2f%%) "
			"- this is the judder count, 0%% is smooth\n",
			pcvr_ageChanges, pcvr_ageCount,
			( 100.0 * pcvr_ageChanges ) / (double)pcvr_ageCount );
	common->Printf( "PCVR pose age histogram (ms: count):" );
	for ( int i = 0; i < PCVR_AGE_MAX; i++ ) {
		if ( pcvr_ageHist[ i ] > 0 ) {
			common->Printf( " %d:%d", i, pcvr_ageHist[ i ] );
		}
	}
	common->Printf( "\n" );

	common->Printf( "PCVR submit interval (compositor cadence, ms: count):" );
	for ( int i = 0; i < PCVR_IVL_MAX; i++ ) {
		if ( pcvr_ivlHist[ i ] > 0 ) {
			common->Printf( " %d:%d", i, pcvr_ivlHist[ i ] );
		}
	}
	if ( pcvr_ivlOver > 0 ) {
		common->Printf( " over:%d", pcvr_ivlOver );
	}
	common->Printf( "\n" );
}

/*
================
PCVR: never print from the render backend.

idCommonLocal::VPrintf is not thread safe and never claimed to be. It appends to
the shared console buffer, guards its own re-entry with a plain static bool, and
can open and write a file. The main thread calls it constantly - a level load
alone prints hundreds of lines - so anything calling it from the backend is
racing it.

Three freezes in feedingtowera said so. Each one hung with the backend inside
RB_SetBuffer, which is GLimp_SetupFrame, which is Doom3Quest_prepareEyeBuffer -
and each one hung on the exact frame the composition layer switched:

    froze at frame 36761   last layer switch 36761
    froze at frame 39649   last layer switch 39649
    froze at frame  8600   last layer switch  8600

The only thing that happens solely on a switch is PCVR_NoteLayerSwitch, and it
called common->Printf. During a level load, with the main thread deep in its own
printing, that is a collision every time the two coincide.

So the backend queues a line and the main thread prints it. A ring of sixteen is
far more than a frame ever produces; if it ever did overflow, losing a log line
is the correct thing to lose.
================
*/
#define PCVR_DEFER_SLOTS 16
#define PCVR_DEFER_LEN   256
static char				pcvr_deferBuf[ PCVR_DEFER_SLOTS ][ PCVR_DEFER_LEN ];
static volatile LONG	pcvr_deferWrite = 0;
static LONG				pcvr_deferRead = 0;

static void PCVR_Defer( const char *line ) {
	const LONG slot = InterlockedIncrement( &pcvr_deferWrite ) - 1;

	strncpy( pcvr_deferBuf[ slot % PCVR_DEFER_SLOTS ], line, PCVR_DEFER_LEN - 1 );
	pcvr_deferBuf[ slot % PCVR_DEFER_SLOTS ][ PCVR_DEFER_LEN - 1 ] = 0;
}

// Called once a frame from idCommonLocal::Frame - the main thread, and the only
// thread allowed to print.
extern "C" void PCVR_DrainDeferred( void ) {
	const LONG w = pcvr_deferWrite;

	if ( pcvr_deferRead < w - PCVR_DEFER_SLOTS ) {
		pcvr_deferRead = w - PCVR_DEFER_SLOTS;
	}
	while ( pcvr_deferRead < w ) {
		common->Printf( "%s\n", pcvr_deferBuf[ pcvr_deferRead % PCVR_DEFER_SLOTS ] );
		pcvr_deferRead++;
	}
}

extern "C" void PCVR_NoteLayerSwitch( const char *to ) {
	char line[ PCVR_DEFER_LEN ];

	pcvr_layerSwitches++;
	pcvr_lastLayer = to;
	pcvr_lastLayerFrame = (int)pcvr_frameSerial;
	idStr::snPrintf( line, sizeof( line ), "PCVR composition layer -> %s at frame %d (switch #%ld)",
					 to, pcvr_lastLayerFrame, pcvr_layerSwitches );
	PCVR_Defer( line );
}

extern "C" void PCVR_NoteRebuild( const char *where ) {
	char line[ PCVR_DEFER_LEN ];

	pcvr_rebuilds++;
	pcvr_lastRebuildWhere = where;
	pcvr_lastRebuildFrame = (int)pcvr_frameSerial;
	idStr::snPrintf( line, sizeof( line ), "PCVR renderer rebuild #%ld at frame %d - %s",
					 pcvr_rebuilds, pcvr_lastRebuildFrame, where );
	PCVR_Defer( line );
}

extern "C" void PCVR_Stage( const char *what ) {
	const DWORD	me = GetCurrentThreadId();
	const int	now = Sys_Milliseconds();
	int			i;
	int			free = -1;

	pcvr_stage = what;
	pcvr_stageThread = me;
	pcvr_stageMs = now;

	for ( i = 0; i < PCVR_MAX_STAGE_THREADS; i++ ) {
		if ( pcvr_stages[ i ].thread == me ) {
			pcvr_stages[ i ].what = what;
			pcvr_stages[ i ].ms = now;
			return;
		}
		if ( free < 0 && pcvr_stages[ i ].thread == 0 ) {
			free = i;
		}
	}

	if ( free >= 0 ) {
		pcvr_stages[ free ].thread = me;
		pcvr_stages[ free ].what = what;
		pcvr_stages[ free ].ms = now;
	}
}

extern "C" void PCVR_NoteBackendThread( void ) {
	pcvr_backendThreadId = GetCurrentThreadId();
}

/*
================
PCVR_GetCPUGHz

What SetMachineSpec is actually asking for. It reads Sys_ClockTicksPerSecond
and calls the answer GHz, which is true on a build whose clock ticks are
rdtsc and false here, where they are QueryPerformanceCounter - 10 MHz on
every modern Windows, so a 3.7 GHz part reported 0.01 GHz and no machine
could qualify for anything above Low.

The processor's advertised speed is in the registry, put there by the HAL at
boot. No measurement, no rdtsc, no dependence on the timer base.
================
*/
extern "C" double PCVR_GetCPUGHz( void ) {
	HKEY	key;
	DWORD	mhz = 0;
	DWORD	size = sizeof( mhz );
	DWORD	type = 0;

	if ( RegOpenKeyExA( HKEY_LOCAL_MACHINE,
						"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
						0, KEY_READ, &key ) == ERROR_SUCCESS ) {
		if ( RegQueryValueExA( key, "~MHz", NULL, &type, (LPBYTE)&mhz, &size ) != ERROR_SUCCESS ) {
			mhz = 0;
		}
		RegCloseKey( key );
	}

	if ( mhz == 0 ) {
		return 0.0;
	}

	return (double)mhz / 1000.0;
}

/*
================
Sys_GetVideoRam

Theirs is `return 256` in sys/glimp.cpp - an Android stub, and the second
half of why every machine detected as Low. DXGI knows the real figure and is
loaded dynamically so the build gains no import and no link dependency.

Returns 0 when it cannot tell, rather than a flattering guess: a number
invented here would be indistinguishable from a measured one in the log.
================
*/
typedef HRESULT ( WINAPI *PCVR_CreateDXGIFactory1_t )( REFIID, void ** );

int Sys_GetVideoRam( void ) {
	static int	cached = -1;

	HMODULE						dxgi;
	PCVR_CreateDXGIFactory1_t	create;
	IDXGIFactory1				*factory = NULL;
	IDXGIAdapter1				*adapter = NULL;
	SIZE_T						best = 0;
	UINT						i;

	if ( cached >= 0 ) {
		return cached;
	}
	cached = 0;

	dxgi = LoadLibraryA( "dxgi.dll" );
	if ( !dxgi ) {
		return cached;
	}

	create = (PCVR_CreateDXGIFactory1_t)GetProcAddress( dxgi, "CreateDXGIFactory1" );
	if ( create && SUCCEEDED( create( __uuidof( IDXGIFactory1 ), (void **)&factory ) ) && factory ) {
		for ( i = 0; factory->EnumAdapters1( i, &adapter ) != DXGI_ERROR_NOT_FOUND; i++ ) {
			DXGI_ADAPTER_DESC1 desc;

			if ( adapter && SUCCEEDED( adapter->GetDesc1( &desc ) ) ) {
				// Skip the software adapter, and take the largest real one -
				// a laptop reports its integrated GPU as well as its discrete.
				if ( !( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) &&
					 desc.DedicatedVideoMemory > best ) {
					best = desc.DedicatedVideoMemory;
				}
			}
			if ( adapter ) {
				adapter->Release();
				adapter = NULL;
			}
		}
		factory->Release();
	}

	FreeLibrary( dxgi );

	cached = (int)( best / ( 1024 * 1024 ) );
	return cached;
}


extern "C" void PCVR_LogSystemInfo( void ) {
	MEMORYSTATUSEX	mem;
	SYSTEM_INFO		si;
	HKEY			key;
	char			product[ 256 ] = "";
	char			display[ 64 ] = "";
	char			build[ 64 ] = "";
	char			cpu[ 64 ] = "";
	int				regs[ 4 ];
	int				i;

	common->Printf( "---------------- this machine ----------------\n" );
	common->Printf( "PreyVR PCVR, built %s %s\n", __DATE__, __TIME__ );

	// The version string Windows reports through GetVersionEx is capped for
	// compatibility unless the exe carries a manifest saying otherwise, so it
	// would report 8.1 on Windows 11. The registry is not capped.
	if ( RegOpenKeyExA( HKEY_LOCAL_MACHINE,
						"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
						0, KEY_READ, &key ) == ERROR_SUCCESS ) {
		DWORD size;

		size = sizeof( product );
		RegQueryValueExA( key, "ProductName", NULL, NULL, (LPBYTE)product, &size );
		size = sizeof( display );
		RegQueryValueExA( key, "DisplayVersion", NULL, NULL, (LPBYTE)display, &size );
		size = sizeof( build );
		RegQueryValueExA( key, "CurrentBuild", NULL, NULL, (LPBYTE)build, &size );
		RegCloseKey( key );
	}
	// ProductName still reads "Windows 10 Pro" on Windows 11 - Microsoft never
	// changed the value, and a diagnostic that names the wrong OS is worse than
	// one that names none. The build number is what actually moved.
	if ( atoi( build ) >= 22000 && idStr::Icmpn( product, "Windows 10", 10 ) == 0 ) {
		char fixed[ 256 ];

		idStr::snPrintf( fixed, sizeof( fixed ) - 1, "Windows 11%s", product + 10 );
		idStr::Copynz( product, fixed, sizeof( product ) );
	}

	common->Printf( "OS        %s %s (build %s)\n",
					product[ 0 ] ? product : "Windows",
					display, build );

	// The processor's own brand string, out of CPUID leaves 0x80000002-4.
	__cpuid( regs, 0x80000000 );
	if ( (unsigned int)regs[ 0 ] >= 0x80000004 ) {
		for ( i = 0; i < 3; i++ ) {
			__cpuid( regs, 0x80000002 + i );
			memcpy( cpu + i * 16, regs, 16 );
		}
		cpu[ 48 ] = 0;
	}

	GetSystemInfo( &si );
	common->Printf( "CPU       %s (%lu threads)\n",
					cpu[ 0 ] ? cpu : "unknown",
					(unsigned long)si.dwNumberOfProcessors );

	memset( &mem, 0, sizeof( mem ) );
	mem.dwLength = sizeof( mem );
	if ( GlobalMemoryStatusEx( &mem ) ) {
		common->Printf( "RAM       %llu MB total, %llu MB free\n",
						(unsigned long long)( mem.ullTotalPhys / ( 1024 * 1024 ) ),
						(unsigned long long)( mem.ullAvailPhys / ( 1024 * 1024 ) ) );
	}

	common->Printf( "mode      %s\n", PCVR_Flatscreen() ? "flatscreen (no VR runtime)" : "VR" );
	common->Printf( "exe       %s\n", Sys_EXEPath() );
	common->Printf( "cmdline   %s\n", GetCommandLineA() );
	common->Printf( "----------------------------------------------\n" );
}

extern "C" void PCVR_StartWatchdog( void ) {
	HANDLE th;

	pcvr_mainThreadId = GetCurrentThreadId();

	// Up here, while everything is calm. Doing it from inside the report, with
	// the process suspended, is how a hang detector becomes a second hang.
	SymSetOptions( SYMOPT_DEFERRED_LOADS );
	SymInitialize( GetCurrentProcess(), NULL, FALSE );
	SymRefreshModuleList( GetCurrentProcess() );

	th = CreateThread( NULL, 0, PCVR_WatchdogProc, NULL, 0, NULL );
	if ( th ) {
		CloseHandle( th );
	}
}

