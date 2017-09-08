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
    struct semaphoreid_t
    {
        HANDLE      sem;
    };

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

    inline void SemaphoreCreate( semaphoreid_t& semaphoreid )
    {
        semaphoreid.sem = CreateSemaphore(NULL, 0, MAXLONG, NULL );
    }

    inline void SemaphoreClose( semaphoreid_t& semaphoreid )
    {
        CloseHandle( semaphoreid.sem );
    }

    inline void SemaphoreWait( semaphoreid_t& semaphoreid  )
    {
        DWORD retval = WaitForSingleObject( semaphoreid.sem, INFINITE );

        assert( retval != WAIT_FAILED );
    }

    inline void SemaphoreSignal( semaphoreid_t& semaphoreid, int32_t countWaiting )
    {
		if( countWaiting )
		{
			ReleaseSemaphore( semaphoreid.sem, countWaiting, NULL );
		}
    }
}

#else // posix

	#include <pthread.h>
	#include <semaphore.h>
	#include <unistd.h>
	#define THREADFUNC_DECL void*
	#define THREAD_LOCAL __thread

namespace enki
{
    typedef pthread_t threadid_t;
    struct semaphoreid_t
    {
        sem_t   sem;
    };
    
        
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
    
    inline void SemaphoreCreate( semaphoreid_t& semaphoreid )
    {
		int err = sem_init( &semaphoreid.sem, 0, 0 );
		assert( err == 0 );
    }
    
    inline void SemaphoreClose( semaphoreid_t& semaphoreid )
    {
        sem_destroy( &semaphoreid.sem );
    }
    
    inline void SemaphoreWait( semaphoreid_t& semaphoreid  )
    {
        int err = sem_wait( &semaphoreid.sem );
		assert( err == 0 );
    }
    
    inline void SemaphoreSignal( semaphoreid_t& semaphoreid, int32_t countWaiting )
    {
        while( countWaiting-- > 0 )
		{
			sem_post( &semaphoreid.sem );
		}
    }
}

#endif // posix

