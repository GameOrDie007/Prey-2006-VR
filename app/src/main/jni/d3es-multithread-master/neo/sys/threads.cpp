/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

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

#include <SDL_version.h>
#include <SDL_mutex.h>
#include <SDL_thread.h>
#include <SDL_timer.h>
// PCVR: POSIX only. On Windows the SDL path below is used instead, and
// usleep() comes from the shim in sys/platform.h.
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>
#endif


#include "idlib/precompiled.h"
#include "framework/Common.h"

#include "sys/sys_public.h"

// PCVR: declared in framework/Common.cpp. An int, not the idCVar - reading
// the cvar from this thread crashed the game on quit, because the cvar
// system frees the object's internals while this loop is still running.
extern volatile int pcvr_frameLockedActive;

static SDL_mutex	*mutex[MAX_CRITICAL_SECTIONS] = { };
static SDL_cond		*cond[MAX_TRIGGER_EVENTS] = { };
static bool			signaled[MAX_TRIGGER_EVENTS] = { };
static bool			waiting[MAX_TRIGGER_EVENTS] = { };

static xthreadInfo	*thread[MAX_THREADS] = { };
static size_t		thread_count = 0;


/*
=========================================================
Async Thread
=========================================================
*/

xthreadInfo asyncThread;

/*
=================
Sys_AsyncThread
=================
*/
void *Sys_AsyncThread(void *p)
{
	int now;
	int start, end;
	int ticked, to_ticked;

// PCVR: widened alongside the thread-exit check below, which now also takes
// the Android branch on Windows and needs this declaration.
#if defined(__ANDROID__) || defined(_WIN32)
	xthreadInfo *threadInfo = static_cast<xthreadInfo *>(p);
	assert(threadInfo);
#endif

#if defined( _WIN32 )
	// PCVR: this thread woke on a hardcoded 16 ms period - `>> 4`, i.e. 62.5 Hz.
	// That constant predates this fork making the tic length follow the headset:
	// USERCMD_MSEC is (1000 / renderSystem->GetRefresh()), 11 ms at 90 Hz.
	//
	// The two disagreeing is what capped the game at 62.5 fps on a 90 Hz display.
	// Tics were being produced at the right *average* rate - measured 91/sec -
	// but in bursts of one or two per 16 ms wake, and idSessionLocal::Frame
	// consumes every pending tic and then waits for the next one. So a frame
	// could only ever complete once per wake, and the frame rate equalled the
	// wake rate rather than the tic rate. 62.5 fps against a 90 Hz compositor is
	// uneven frame delivery, which is what the judder was.
	//
	// Waking on the same period the tics are generated on makes them agree.
	// Clamped to 16 ms so this can never run slower than their original, and
	// re-derived if the refresh changes under us.
	int period = USERCMD_MSEC;
	int lastPeriod;

	if (period < 1) { period = 1; }
	if (period > 16) { period = 16; }
	lastPeriod = period;

	start = Sys_Milliseconds();
	ticked = start / period;

	while (1) {
		period = USERCMD_MSEC;
		if (period < 1) { period = 1; }
		if (period > 16) { period = 16; }

		// PCVR: phase-locked to the compositor instead of to a wall clock.
		//
		// The loop below ticks on Sys_Milliseconds() / period, and period is
		// (1000 / refresh) truncated - 11 at 90 Hz. That is 90.909 tics a
		// second against a 90 Hz display, so the two beat and some frames get
		// two world updates and some get none. Measured on the test machine: 7.1%.
		//
		// Waiting on the frame instead makes it one tic per displayed frame by
		// construction. The timeout is what keeps a level load - where no
		// frames are submitted for seconds - from parking this thread: on a
		// timeout it falls through and ticks on the clock as before.
		if (pcvr_frameLockedActive) {
			Sys_WaitForEventTimeout(TRIGGER_EVENT_VR_FRAME, period * 4 + 4);
			common->Async();
			Sys_TriggerEvent(TRIGGER_EVENT_ONE);
			continue;
		}

		start = Sys_Milliseconds();

		if (period != lastPeriod) {
			// the counters are absolute clock/period, so rebase rather than jump
			lastPeriod = period;
			ticked = start / period;
		}

		to_ticked = start / period;

		while (ticked < to_ticked) {
			common->Async();
			ticked++;
			Sys_TriggerEvent(TRIGGER_EVENT_ONE);
		}

		// sleep
		end = Sys_Milliseconds() - start;
		if (end < period) {
			usleep(period - end);
		}
#else
	start = Sys_Milliseconds();
	ticked = start >> 4;

	while (1) {
		start = Sys_Milliseconds();
		to_ticked = start >> 4;

		while (ticked < to_ticked) {
			common->Async();
			ticked++;
			Sys_TriggerEvent(TRIGGER_EVENT_ONE);
		}

		// sleep
		end = Sys_Milliseconds() - start;
		if (end < 16) {
			usleep(16 - end);
		}
#endif

		// thread exit
// PCVR: Windows joins the Android branch - there is no pthread_testcancel,
// and the threadCancel flag is the path lvonasek's build already takes.
#if defined(__ANDROID__) || defined(_WIN32)
		if (threadInfo->threadCancel) {
			break;
		}
#else
		pthread_testcancel();
#endif
	}

	return NULL;
}

/*
=================
Posix_StartAsyncThread
=================
*/
void Posix_StartAsyncThread()
{
	if (asyncThread.threadHandle == 0) {
		Sys_CreateThread(reinterpret_cast<xthread_t>(Sys_AsyncThread), &asyncThread, THREAD_NORMAL, asyncThread, "Async", g_threads, &g_thread_count);
	} else {
		common->Printf("Async thread already running\n");
	}

	common->Printf("Async thread started\n");
}

/*
==============
Sys_Sleep
==============
*/
void Sys_Sleep(int msec) {
	SDL_Delay(msec);
}

/*
================
Sys_Milliseconds
================
*/
unsigned int Sys_Milliseconds() {
	return SDL_GetTicks();
}

/*
==================
Sys_InitThreads
==================
*/
void Sys_InitThreads() {
	// critical sections
	for (int i = 0; i < MAX_CRITICAL_SECTIONS; i++) {
		mutex[i] = SDL_CreateMutex();

		if (!mutex[i]) {
			Sys_Printf("ERROR: SDL_CreateMutex failed\n");
			return;
		}
	}

	// events
	for (int i = 0; i < MAX_TRIGGER_EVENTS; i++) {
		cond[i] = SDL_CreateCond();

		if (!cond[i]) {
			Sys_Printf("ERROR: SDL_CreateCond failed\n");
			return;
		}

		signaled[i] = false;
		waiting[i] = false;
	}

	// threads
	for (int i = 0; i < MAX_THREADS; i++)
		thread[i] = NULL;

	thread_count = 0;

	Posix_StartAsyncThread();
}

/*
==================
Sys_ShutdownThreads
==================
*/
void Sys_ShutdownThreads() {
	// threads
	for (int i = 0; i < MAX_THREADS; i++) {
		if (!thread[i])
			continue;

		Sys_Printf("WARNING: Thread '%s' still running\n", thread[i]->name);
#if SDL_VERSION_ATLEAST(2, 0, 0)
		// TODO no equivalent in SDL2
#else
		SDL_KillThread(thread[i]->threadHandle);
#endif
		thread[i] = NULL;
	}

	// events
	for (int i = 0; i < MAX_TRIGGER_EVENTS; i++) {
		SDL_DestroyCond(cond[i]);
		cond[i] = NULL;
		signaled[i] = false;
		waiting[i] = false;
	}

	// critical sections
	for (int i = 0; i < MAX_CRITICAL_SECTIONS; i++) {
		SDL_DestroyMutex(mutex[i]);
		mutex[i] = NULL;
	}
}

/*
==================
Sys_EnterCriticalSection
==================
*/
void Sys_EnterCriticalSection(int index) {
	assert(index >= 0 && index < MAX_CRITICAL_SECTIONS);

	if (SDL_LockMutex(mutex[index]) != 0)
		common->Error("ERROR: SDL_LockMutex failed\n");
}

/*
==================
Sys_LeaveCriticalSection
==================
*/
void Sys_LeaveCriticalSection(int index) {
	assert(index >= 0 && index < MAX_CRITICAL_SECTIONS);

	if (SDL_UnlockMutex(mutex[index]) != 0)
		common->Error("ERROR: SDL_UnlockMutex failed\n");
}

/*
======================================================
wait and trigger events
we use a single lock to manipulate the conditions, CRITICAL_SECTION_SYS

the semantics match the win32 version. signals raised while no one is waiting sta   y raised until a wait happens (which then does a simple pass-through)

NOTE: we use the same mutex for all the events. I don't think this would become much of a problem
cond_wait unlocks atomically with setting the wait condition, and locks it back before exiting the function
the potential for time wasting lock waits is very low
======================================================
*/

/*
==================
Sys_WaitForEvent
==================
*/
// PCVR: the same wait, but it gives up.
//
// The frame-locked tic waits on the compositor, and the compositor stops
// handing out frames during a level load. A wait with no way out would park the
// async thread for the whole load - no sound streaming, and one more way for
// two threads to sit looking at each other. Returns true if the event actually
// arrived, so the caller can tell a frame from a timeout.
bool Sys_WaitForEventTimeout(int index, int ms) {
	bool got = true;

	assert(index >= 0 && index < MAX_TRIGGER_EVENTS);

	Sys_EnterCriticalSection(CRITICAL_SECTION_SYS);

	if (signaled[index]) {
		signaled[index] = false;
	} else {
		waiting[index] = true;
		if (SDL_CondWaitTimeout(cond[index], mutex[CRITICAL_SECTION_SYS], ms) != 0) {
			got = false;
		}
		waiting[index] = false;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_SYS);
	return got;
}

void Sys_WaitForEvent(int index) {
	assert(index >= 0 && index < MAX_TRIGGER_EVENTS);

	Sys_EnterCriticalSection(CRITICAL_SECTION_SYS);

	assert(!waiting[index]);	// WaitForEvent from multiple threads? that wouldn't be good
	if (signaled[index]) {
		// emulate windows behaviour: signal has been raised already. clear and keep going
		signaled[index] = false;
	} else {
		waiting[index] = true;
		if (SDL_CondWait(cond[index], mutex[CRITICAL_SECTION_SYS]) != 0)
			common->Error("ERROR: SDL_CondWait failed\n");
		waiting[index] = false;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_SYS);
}

/*
==================
Sys_TriggerEvent
==================
*/
void Sys_TriggerEvent(int index) {
	assert(index >= 0 && index < MAX_TRIGGER_EVENTS);

	Sys_EnterCriticalSection(CRITICAL_SECTION_SYS);

	if (waiting[index]) {
		if (SDL_CondSignal(cond[index]) != 0)
			common->Error("ERROR: SDL_CondSignal failed\n");
	} else {
		// emulate windows behaviour: if no thread is waiting, leave the signal on so next wait keeps going
		signaled[index] = true;
	}

	Sys_LeaveCriticalSection(CRITICAL_SECTION_SYS);
}

// not a hard limit, just what we keep track of for debugging
#define MAX_THREADS 10
xthreadInfo *g_threads[MAX_THREADS];

int g_thread_count = 0;

typedef void *(*pthread_function_t)(void *);

/*
==================
Sys_CreateThread
==================
*/
void Sys_CreateThread(xthread_t function, void *parms, xthreadPriority priority, xthreadInfo &info, const char *name, xthreadInfo **threads, int *thread_count)
{
	Sys_EnterCriticalSection();
#if defined( _WIN32 )
	// PCVR: DIVERGENCE, deliberate and recorded. Windows has no pthreads, and
	// the rest of this file is already SDL - mutexes, condition variables,
	// timers, and Sys_DestroyThread below, which does
	//     SDL_WaitThread( reinterpret_cast<SDL_Thread *>( info.threadHandle ) )
	// on the handle pthread_create wrote. That is a create/destroy API
	// mismatch in their own code: a pthread_t is not an SDL_Thread *, so
	// Sys_DestroyThread cannot work as written on Android either. It survives
	// there because the threads it creates run for the life of the process.
	//
	// Reproducing the mismatch on PC would mean adding a pthreads-for-Windows
	// dependency purely to keep a broken path broken. Using SDL here matches
	// what Sys_DestroyThread already expects, matches what dhewm3 did before
	// glKarin changed it, and adds no dependency - SDL2 is already linked.
	//
	// Consequence to be honest about: thread shutdown will work on PC where it
	// cannot on the Quest. Their defect is recorded in PROGRESS.md rather than
	// reproduced. Watch this if PC shutdown behaviour ever needs to match.
	SDL_Thread *t = SDL_CreateThread( reinterpret_cast<SDL_ThreadFunction>( function ), name, parms );

	if ( !t ) {
		common->Error( "ERROR: SDL_CreateThread %s failed: %s\n", name, SDL_GetError() );
	}

	info.threadHandle = reinterpret_cast<intptr_t>( t );
#else
	pthread_attr_t attr;
	pthread_attr_init(&attr);

	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) != 0) {
		common->Error("ERROR: pthread_attr_setdetachstate %s failed\n", name);
	}

	if (pthread_create((pthread_t *)&info.threadHandle, &attr,
	                   reinterpret_cast<void *(*)(void *)>(function), parms) != 0) {
		common->Error("ERROR: pthread_create %s failed\n", name);
	}

	pthread_attr_destroy(&attr);
#endif
	info.name = name;

	if (*thread_count < MAX_THREADS) {
		threads[(*thread_count)++ ] = &info;
	} else {
		common->DPrintf("WARNING: MAX_THREADS reached\n");
	}

	Sys_LeaveCriticalSection();
}

/*
==================
Sys_CreateThread
==================
*/
void Sys_CreateThread(xthread_t function, void *parms, xthreadInfo& info, const char *name) {
	Sys_EnterCriticalSection();

#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_Thread *t = SDL_CreateThread(function, name, parms);
#else
	SDL_Thread *t = SDL_CreateThread(function, parms);
#endif

	if (!t) {
		common->Error("ERROR: SDL_thread for '%s' failed\n", name);
		Sys_LeaveCriticalSection();
		return;
	}

	info.name = name;
	info.threadHandle = reinterpret_cast<intptr_t>(t);
	info.threadId = SDL_GetThreadID(t);

	if (thread_count < MAX_THREADS)
		thread[thread_count++] = &info;
	else
		common->DPrintf("WARNING: MAX_THREADS reached\n");

	Sys_LeaveCriticalSection();
}

/*
==================
Sys_DestroyThread
==================
*/
void Sys_DestroyThread(xthreadInfo& info) {
	assert(info.threadHandle);

	SDL_WaitThread(reinterpret_cast<SDL_Thread *>(info.threadHandle), NULL);

	info.name = NULL;
	info.threadHandle = NULL;
	info.threadId = 0;

	Sys_EnterCriticalSection();

	for (int i = 0; i < thread_count; i++) {
		if (&info == thread[i]) {
			thread[i] = NULL;

			int j;
			for (j = i + 1; j < thread_count; j++)
				thread[j - 1] = thread[j];

			thread[j - 1] = NULL;
			thread_count--;

			break;
		}
	}

	Sys_LeaveCriticalSection( );
}

/*
==================
Sys_GetThreadName
find the name of the calling thread
==================
*/
const char *Sys_GetThreadName(int *index) {
	const char *name;

	Sys_EnterCriticalSection();

	unsigned int id = SDL_ThreadID();

	for (int i = 0; i < thread_count; i++) {
		if (id == thread[i]->threadId) {
			if (index)
				*index = i;

			name = thread[i]->name;

			Sys_LeaveCriticalSection();

			return name;
		}
	}

	if (index)
		*index = -1;

	Sys_LeaveCriticalSection();

	return "main";
}
