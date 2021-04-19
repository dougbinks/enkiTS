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

#include "TaskScheduler.h"

#include <stdio.h>
#include <thread>

using namespace enki;

TaskScheduler g_TS;
static std::atomic<int32_t> g_Run;


struct ParallelTaskSet : ITaskSet
{
    ParallelTaskSet() { m_SetSize = 100; }

    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
    {
        bool bIsExternalIOThread = threadnum_ >= enki::TaskScheduler::GetNumFirstExternalTaskThread() &&
                                   threadnum_ <  enki::TaskScheduler::GetNumFirstExternalTaskThread()
                                                 + g_TS.GetConfig().numExternalTaskThreads;
        if( bIsExternalIOThread )
        {
            assert( false ); //for this example this is an error - but external threads can run tasksets in general
            printf(" Run %d: ParallelTaskSet on thread %d which is an IO thread\n", g_Run.load(), threadnum_);
        }
        else
        {
            printf(" Run %d: ParallelTaskSet on thread %d which is not an IO thread\n", g_Run.load(), threadnum_);
        }
    }
};

struct ThreadData
{
    uint32_t externalThreadNum = 0;
    bool     stop              = false; // we do not need an atomic as this is set by a pinned task on the thread it is read from
};

struct StopTask : IPinnedTask
{
    StopTask( ThreadData* pThreadData_ )
        : IPinnedTask( pThreadData_->externalThreadNum + enki::TaskScheduler::GetNumFirstExternalTaskThread() )
        , pThreadData( pThreadData_ )
    {
    }

    void Execute() override
    {
        pThreadData->stop = true;
    }

    ThreadData* pThreadData;
};

struct PretendDoFileIO : IPinnedTask
{
    void Execute() override
    {
        printf(" Run %d: PretendDoFileIO on thread %d\n", g_Run.load(), threadNum );
        // sleep used as a 'pretend' blocking workload
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }
};

struct PretendDoNetworkIO : IPinnedTask
{
    void Execute() override
    {
        printf(" Run %d: PretendDoNetworkIO on thread %d\n", g_Run.load(), threadNum );
        // sleep used as a 'pretend' blocking workload
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }
};

// Example external thread function which waits for pinned tasks
// May want to use threads for blocking IO, during which enkiTS task threads can do work
// An external thread can also use full enkiTS functionality, useful when you have threads
// created by another API you want to use.
void threadFunction( ThreadData* pThreadData_ )
{
    bool bRegistered = g_TS.RegisterExternalTaskThread( pThreadData_->externalThreadNum + enki::TaskScheduler::GetNumFirstExternalTaskThread() );
    assert( bRegistered );
    if( bRegistered )
    {
        while( !pThreadData_->stop )
        {
            g_TS.WaitForNewPinnedTasks(); // this thread will 'sleep' until there are new pinned tasks
            g_TS.RunPinnedTasks();
        }

        g_TS.DeRegisterExternalTaskThread();
    }
}

static const int      REPEATS            = 5;

enum class ExternalThreadId
{
    FILE_IO,            // more than one file io thread may be useful if handling lots of small files
    NETWORK_IO_0,
    NETWORK_IO_1,
    NETWORK_IO_2,
    EXTERNAL_THREAD_NUM,
};

static const uint32_t NUMEXTERNALTHREADS = (uint32_t)ExternalThreadId::EXTERNAL_THREAD_NUM;


int main(int argc, const char * argv[])
{
    enki::TaskSchedulerConfig config;
    config.numExternalTaskThreads = NUMEXTERNALTHREADS;

    std::thread threads[NUMEXTERNALTHREADS];
    g_TS.Initialize( config );

    ThreadData threadData[NUMEXTERNALTHREADS];


    for( g_Run = 0; g_Run< REPEATS; ++g_Run )
    {
        printf("Run %d\n", g_Run.load() );

        for( uint32_t iThread = 0; iThread < NUMEXTERNALTHREADS; ++iThread )
        {
            threadData[ iThread ].externalThreadNum = iThread;
            threadData[ iThread ].stop = false;
            threads[ iThread ] = std::thread( threadFunction, &threadData[ iThread ] );
        }

        // set of a ParallelTaskSet
        // to demonstrate can perform work on enkiTS worker threads but WaitForNewPinnedTasks will
        // not perform work. This can be used to fully subscribe machine with enkiTS worker threads but
        // have extra IO bound threads to handle blocking tasks
        ParallelTaskSet parallelTaskSet;
        g_TS.AddTaskSetToPipe( &parallelTaskSet );

        // Send pretend file IO task to external thread FILE_IO
        PretendDoFileIO pretendDoFileIO;
        pretendDoFileIO.threadNum = (uint32_t)ExternalThreadId::FILE_IO + enki::TaskScheduler::GetNumFirstExternalTaskThread();
        g_TS.AddPinnedTask( &pretendDoFileIO );

        // Send pretend network IO tasks to external thread  NETWORK_IO_0 ... NUMEXTERNALTHREADS
        PretendDoNetworkIO pretendDoNetworkIO[NUMEXTERNALTHREADS-(uint32_t)ExternalThreadId::NETWORK_IO_0];
        for( uint32_t iThread = (uint32_t)ExternalThreadId::NETWORK_IO_0; iThread < NUMEXTERNALTHREADS; ++iThread )
        {
            pretendDoNetworkIO[iThread-(uint32_t)ExternalThreadId::NETWORK_IO_0].threadNum = iThread + enki::TaskScheduler::GetNumFirstExternalTaskThread();
            g_TS.AddPinnedTask( &pretendDoNetworkIO[iThread-(uint32_t)ExternalThreadId::NETWORK_IO_0] );
        }

        for( uint32_t iThread = 0; iThread < NUMEXTERNALTHREADS; ++iThread )
        {
            StopTask stopTask( &threadData[ iThread ] );
            g_TS.AddPinnedTask( &stopTask );
            threads[ iThread ].join();
            assert( stopTask.GetIsComplete() );
        }

        g_TS.WaitforTaskSet( &parallelTaskSet );
    }

    return 0;
}
