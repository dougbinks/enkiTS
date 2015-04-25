// Copyright (c) 2013 Doug Binks
// 
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
// 
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgement in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#pragma once

#include <stdint.h>
#include <assert.h>

#ifdef _WIN32

	#include "Atomics.h"

	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	
	#define THREADFUNC_DECL DWORD WINAPI
	#define THREAD_LOCAL __declspec( thread )

namespace enki
{
    typedef HANDLE threadid_t;
    struct eventid_t
    {
        HANDLE      event;
        int32_t     countWaiters;
    };
    const uint32_t EVENTWAIT_INFINITE = INFINITE;

    // declare the thread start function as:
    // THREADFUNC_DECL MyThreadStart( void* pArg );
    inline bool ThreadCreate( threadid_t* returnid, DWORD ( WINAPI *StartFunc) (void* ), void* pArg )
    {
        // posix equiv pthread_create
        DWORD threadid;
        *returnid = CreateThread( 0, 0, StartFunc, pArg, 0, &threadid );
        return  *returnid != NULL;
    }

    inline bool ThreadTerminate( threadid_t threadid )
    {
        // posix equiv pthread_cancel
        return CloseHandle( threadid ) == 0;
    }

    inline uint32_t GetNumHardwareThreads()
    {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return sysInfo.dwNumberOfProcessors;
    }

    inline eventid_t EventCreate()
    {
        eventid_t ret;
        ret.event = ::CreateEvent( NULL, TRUE, FALSE, NULL );
        ret.countWaiters = 0;
        return ret;
    }

    inline void EventClose( eventid_t eventid )
    {
        CloseHandle( eventid.event );
    }

    inline void EventWait( eventid_t& eventid, uint32_t milliseconds )
    {
        AtomicAdd( &eventid.countWaiters, 1 );
        DWORD retval = WaitForSingleObject( eventid.event, milliseconds );
        int32_t prev = AtomicAdd( &eventid.countWaiters, -1 );
        if( 1 == prev )
        {
            // we were the last to awaken, so reset event.
           ResetEvent( eventid.event );
        }
        assert( retval != WAIT_FAILED );
        assert( prev != 0 );
    }

    inline void EventSignal( eventid_t eventid )
    {
        SetEvent( eventid.event );
    }
}

#else // posix

	#include <pthread.h>
	#include <unistd.h>
	#define THREADFUNC_DECL void*
	#define THREAD_LOCAL __thread

namespace enki
{
    typedef pthread_t threadid_t;
    struct eventid_t
    {
        pthread_cond_t  cond;
        pthread_mutex_t mutex;
    };
    const uint32_t EVENTWAIT_INFINITE = -1;
    
        
    // declare the thread start function as:
    // THREADFUNC_DECL MyThreadStart( void* pArg );
    inline bool ThreadCreate( threadid_t* returnid, void* ( *StartFunc) (void* ), void* pArg )
    {
        // posix equiv pthread_create
        int32_t retval = pthread_create( returnid, NULL, StartFunc, pArg );

        return  retval == 0;
    }
    
    inline bool ThreadTerminate( threadid_t threadid )
    {
        // posix equiv pthread_cancel
        return pthread_cancel( threadid ) == 0;
    }
    
    inline uint32_t GetNumHardwareThreads()
    {
        return (uint32_t)sysconf( _SC_NPROCESSORS_ONLN );
    }
    
    inline eventid_t EventCreate()
    {
        eventid_t event = { PTHREAD_COND_INITIALIZER, PTHREAD_MUTEX_INITIALIZER };
        return event;
    }
    
    inline void EventClose( eventid_t eventid )
    {
        // do not need to close event
    }
    
    inline void EventWait( eventid_t& eventid, uint32_t milliseconds )
    {
        pthread_mutex_lock( &eventid.mutex );
        if( milliseconds == EVENTWAIT_INFINITE )
        {
            pthread_cond_wait( &eventid.cond, &eventid.mutex );
        }
        else
        {
            timespec waittime;
            waittime.tv_sec = milliseconds/1000;
            milliseconds -= waittime.tv_sec*1000;
            waittime.tv_nsec = milliseconds * 1000;
            pthread_cond_timedwait( &eventid.cond, &eventid.mutex, &waittime );

        }
        pthread_mutex_unlock( &eventid.mutex );
    }
    
    inline void EventSignal( eventid_t& eventid )
    {
        pthread_mutex_lock( &eventid.mutex );
        pthread_cond_broadcast( &eventid.cond );
        pthread_mutex_unlock( &eventid.mutex );
    }
}

#endif // posix

