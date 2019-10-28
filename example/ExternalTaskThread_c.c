// Copyright (c) 2019 Doug Binks
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
enkiTaskSet*          pParallelTask;


void ParallelFunc( uint32_t start_, uint32_t end, uint32_t threadnum_, void* pArgs_ )
{
    // do something
    printf("ParallelFunc running on thread %d (could be any thread)\n", threadnum_ );
}

THREADFUNC_DECL ThreadFunc( void* pArgs_ )
{
    uint32_t threadNum;
    int retVal;

    retVal = enkiRegisterExternalTaskThread( pETS );
    assert( retVal );

    threadNum = enkiGetThreadNum( pETS );
    assert( threadNum == 1 );
    printf("ThreadFunc running on thread %d (should be thread 1)\n", threadNum );

    pParallelTask = enkiCreateTaskSet( pETS, ParallelFunc );

    enkiAddTaskSetToPipe( pETS, pParallelTask, NULL, 1);
    enkiWaitForTaskSet( pETS, pParallelTask );

    enkiDeleteTaskSet( pParallelTask );

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

    ThreadJoin( threadID );


    enkiDeleteTaskScheduler( pETS );
}