/*
===========================================================================
PCVR: the instrument hooks, and nothing at all on Android.

The VR layer calls these from VrRenderer.c and VrFramebuffer.c to record what
the renderer is doing - the stage the backend is in, the OpenXR frame call
counts, which head pose the game used. They are what turned "it froze" into a
named call, and they are worth keeping in the shipped build: they cost a
pointer store and an interlocked increment.

But they are DEFINED in sys/win32/win_pcvr.cpp, which is Windows only, while
the two files that call them are shared with the Android build. Declared and
called unconditionally, they compile on Windows and fail to link on Android -
and the one thing this port promises about their tree is that it still builds.

So: real declarations on Windows, empty macros everywhere else. The calls stay
where they are and read the same in both builds.

The Windows compiler found this as C4013, "'PCVR_Stage' undefined; assuming
extern returning int" - an implicit declaration that happened to work because
x64 passes the argument in a register either way. Worth remembering that the
warning was the only thing that noticed.
===========================================================================
*/

#ifndef PCVR_INSTRUMENT_H
#define PCVR_INSTRUMENT_H

#if defined( _WIN32 )

// They are defined extern "C" in win_pcvr.cpp. Every caller used to be a .c
// file, where that matched by default - then tr_subview.cpp included this and
// the link failed on a mangled name. Declare the linkage rather than relying
// on who happens to include it.
#ifdef __cplusplus
extern "C" {
#endif

// sys/win32/win_pcvr.cpp
void PCVR_Stage( const char *what );        // where the renderer is, for freeze.txt
void PCVR_CountXr( int which );             // 0 wait started, 1 wait returned, 2 begin, 3 end
void PCVR_NoteLocate( void );               // a new head pose exists
void PCVR_NotePoseGen( long gen );          // the pose the projection layer declares
void PCVR_NoteGamePoseGen( void );          // the pose the game's tic used
void PCVR_NoteEyeSize( int w, int h );      // the eye buffer size
void PCVR_NoteRebuild( const char *where ); // a renderer rebuild, and where from
void PCVR_NoteLayerSwitch( const char *to );// a composition layer change
void PCVR_NotePoseAge( int ms );            // how stale the submitted picture is
void PCVR_NoteSubmitInterval( int ms );     // gap between the display times we claim
void PCVR_NoteHaptic( int us );             // how long a haptic call held the main thread
void PCVR_NoteWaitReturn( void );           // the compositor released us
void PCVR_NoteBeginFrameTime( void );       // drawing starts here
void PCVR_NoteSubmit( void );               // we handed the frame back
void PCVR_XrIn( void );                     // an OpenXR call starts
void PCVR_XrOut( int which );               // ... and how long it took
void PCVR_Log( const char *line );          // into qconsole.log, not stdout
void PCVR_NotePortalSubview( int beyondMapLimit ); // an extra scene render, and why
void PCVR_MirrorBegin( void );              // the desktop mirror starts
void PCVR_MirrorEnd( void );                // ... and what it cost
// which: 0 begin, 1 acquire, 2 wait-image, 3 release, 4 endFrame

#ifdef __cplusplus
}
#endif

#else

#define PCVR_Stage( what )              ( (void)0 )
#define PCVR_CountXr( which )           ( (void)0 )
#define PCVR_NoteLocate()               ( (void)0 )
#define PCVR_NotePoseGen( gen )         ( (void)0 )
#define PCVR_NoteGamePoseGen()          ( (void)0 )
#define PCVR_NoteEyeSize( w, h )        ( (void)0 )
#define PCVR_NoteRebuild( where )       ( (void)0 )
#define PCVR_NoteLayerSwitch( to )      ( (void)0 )
#define PCVR_NotePoseAge( ms )          ( (void)0 )
#define PCVR_NoteSubmitInterval( ms )   ( (void)0 )
#define PCVR_NoteHaptic( us )           ( (void)0 )
#define PCVR_NoteWaitReturn()           ( (void)0 )
#define PCVR_NoteBeginFrameTime()       ( (void)0 )
#define PCVR_NoteSubmit()               ( (void)0 )
#define PCVR_XrIn()                     ( (void)0 )
#define PCVR_XrOut( which )             ( (void)0 )
#define PCVR_Log( line )                ( (void)0 )
#define PCVR_NotePortalSubview( b )     ( (void)0 )
#define PCVR_MirrorBegin()              ( (void)0 )
#define PCVR_MirrorEnd()                ( (void)0 )

#endif

#endif
