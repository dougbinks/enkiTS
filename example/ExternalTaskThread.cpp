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

#include "TaskScheduler.h"

#include <stdio.h>
#include <thread>

using namespace enki;

TaskScheduler g_TS;
static std::atomic<int32_t> g_Run;


struct ParallelTaskSet : ITaskSet
{
    ParallelTaskSet() { m_SetSize = 100; }

    virtual void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ )
    {
        printf(" Run %d: This could run on any thread, currently thread %d\n", g_Run.load(), threadnum_);

        // sleep used as a 'pretend' workload
        std::chrono::milliseconds sleepTime( range_.end - range_.start );
        std::this_thread::sleep_for( sleepTime );
    }
};

// Example thread function
// May want to use threads for blocking IO, during which enkiTS task threads can do work
void threadFunction( uint32_t num_ )
{
    bool bRegistered = g_TS.RegisterExternalTaskThread();
    assert( bRegistered );
    if( bRegistered )
    {
        // sleep for a while instead of doing something such as file IO
        std::this_thread::sleep_for( std::chrono::milliseconds( num_ * 100 ) );


        ParallelTaskSet task;
        g_TS.AddTaskSetToPipe( &task );
        g_TS.WaitforTask( &task);
        g_TS.DeRegisterExternalTaskThread();
    }
}

static const int      REPEATS            = 5;
static const uint32_t NUMEXTERNALTHREADS = 5; 

int main(int argc, const char * argv[])
{
    enki::TaskSchedulerConfig config;
    config.numExternalTaskThreads = NUMEXTERNALTHREADS;

    std::thread threads[NUMEXTERNALTHREADS];
    g_TS.Initialize( config );


    for( g_Run = 0; g_Run< REPEATS; ++g_Run )
    {
        printf("Run %d\n", g_Run.load() );

        for( uint32_t iThread = 0; iThread < NUMEXTERNALTHREADS; ++iThread )
        {
            threads[ iThread ] = std::thread( threadFunction, iThread );
        }

        // check that out of order Deregister / Register works...
        threads[ 0 ].join();
        threads[ 0 ] = std::thread( threadFunction, 0 );

        for( uint32_t iThread = 0; iThread < NUMEXTERNALTHREADS; ++iThread )
        {
            threads[ iThread ].join();
        }
    }

    return 0;
}
