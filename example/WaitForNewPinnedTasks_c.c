// Copyright (c) 2021 Doug Binks
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

#include "TaskScheduler_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// XPLATF Thread handling functions for C
#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
    
typedef HANDLE threadid_t;
#define THREADFUNC_DECL DWORD WINAPI

// declare the thread start function as:
// THREADFUNC_DECL MyThreadStart( void* pArg );
int32_t ThreadCreate( threadid_t* returnid, DWORD ( WINAPI *StartFunc) (void* ), void* pArg )
{
    DWORD threadid;
    *returnid = CreateThread( 0, 0, StartFunc, pArg, 0, &threadid );
    return  *returnid != NULL;
}

int32_t ThreadJoin( threadid_t threadid )
{
    return WaitForSingleObject( threadid, INFINITE ) == 0;
}

#else // posix
#include <pthread.h>
#include <unistd.h>

typedef pthread_t threadid_t;  
#define THREADFUNC_DECL void*

// declare the thread start function as:
// THREADFUNC_DECL MyThreadStart( void* pArg );
int32_t ThreadCreate( threadid_t* returnid, void* ( *StartFunc) (void* ), void* pArg )
{
    int32_t retval = pthread_create( returnid, NULL, StartFunc, pArg );

    return  retval == 0;
}
    
int32_t ThreadJoin( threadid_t threadid )
{
    return pthread_join( threadid, NULL ) == 0;
}
#endif

enkiTaskScheduler*    pETS;
enkiPinnedTask*       pPinnedTaskPretendIO;
enkiPinnedTask*       pPinnedTaskStop;


void PinnedTaskPretendIOFunc( void* pArgs_ )
{
    int32_t dataVal = *(int32_t*)pArgs_;
    printf("Run %d: Example PinnedTaskPretendIOFunc - this could perform network or file IO\n", dataVal );
}

int32_t g_Stop = 0; // we do not need an atomic as this is set by a pinned task on the thread it is read from

void PinnedTaskStopFunc( void* pArgs_ )
{
    g_Stop = 1; // call stop
}

THREADFUNC_DECL ThreadFunc( void* pArgs_ )
{
    uint32_t threadNum;
    int retVal;

    // we register to first address
    retVal = enkiRegisterExternalTaskThreadNum( pETS, enkiGetNumFirstExternalTaskThread() );
    assert( retVal );

    threadNum = enkiGetThreadNum( pETS );
    assert( threadNum == enkiGetNumFirstExternalTaskThread() );
    printf("ThreadFunc running on thread %d (should be thread %d)\n", threadNum, enkiGetNumFirstExternalTaskThread() );

    while( 0 == g_Stop )
    {
        enkiWaitForNewPinnedTasks( pETS );
        enkiRunPinnedTasks( pETS );
    }

    enkiDeRegisterExternalTaskThread( pETS );
    return 0;
}

int main(int argc, const char * argv[])
{
    struct enkiTaskSchedulerConfig config;

    pETS = enkiNewTaskScheduler();

    // get default config and request one external thread
    config = enkiGetTaskSchedulerConfig( pETS );
    config.numExternalTaskThreads = 1;
    enkiInitTaskSchedulerWithConfig( pETS, config );

    threadid_t threadID;
    ThreadCreate( &threadID, ThreadFunc, NULL );

    // send pretend IO commands to external thread
    pPinnedTaskPretendIO = enkiCreatePinnedTask( pETS, PinnedTaskPretendIOFunc, enkiGetNumFirstExternalTaskThread() );
    for( int32_t i=0; i<5; ++i )
    {
        // we re-use one task here as we are waiting for each to complete
        enkiAddPinnedTaskArgs( pETS, pPinnedTaskPretendIO, &i );

        // in most real world cases you would not wait for pinned IO task immediatly after
        // issueing it, but instead do work.
        // Rather than waiting can use dependencies or issue a pinned task to main thread (id 0) to send data
        enkiWaitForPinnedTask( pETS, pPinnedTaskPretendIO );
    }
    enkiDeletePinnedTask( pETS, pPinnedTaskPretendIO );

    // send stop to external thread
    pPinnedTaskStop = enkiCreatePinnedTask( pETS, PinnedTaskStopFunc, enkiGetNumFirstExternalTaskThread() );
    enkiAddPinnedTask( pETS, pPinnedTaskStop );


    ThreadJoin( threadID );
    enkiDeletePinnedTask( pETS, pPinnedTaskStop );


    enkiDeleteTaskScheduler( pETS );
    printf("WaitForNewPinnedTasks_c.c completed\n" );

}
