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

using namespace enki;

TaskScheduler g_TS;
static std::atomic<int32_t> g_Run;

enum class IOThreadId
{
    FILE_IO,            // more than one file io thread may be useful if handling lots of small files
    NETWORK_IO_0,
    NETWORK_IO_1,
    NETWORK_IO_2,
    NUM,
};

struct ParallelTaskSet : ITaskSet
{
    ParallelTaskSet() { m_SetSize = 100; }

    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
    {
        bool bIsIOThread = threadnum_ >= g_TS.GetNumTaskThreads() - (uint32_t)IOThreadId::NUM;
        if( bIsIOThread )
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

struct RunPinnedTaskLoopTask : IPinnedTask
{
    void Execute() override
    {
        while( !g_TS.GetIsShutdownRequested() )
        {
            g_TS.WaitForNewPinnedTasks(); // this thread will 'sleep' until there are new pinned tasks
            g_TS.RunPinnedTasks();
        }
    }
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

static const int      REPEATS            = 5;

int main(int argc, const char * argv[])
{
    enki::TaskSchedulerConfig config;

    // In this example we create more threads than the hardware can run,
    // because the IO threads will spend most of their time idle or blocked
    // and therefore not scheduled for CPU time by the OS
    config.numTaskThreadsToCreate += (uint32_t)IOThreadId::NUM;

    g_TS.Initialize( config );

    // in this example we place our IO threads at the end
    uint32_t theadNumIOStart = g_TS.GetNumTaskThreads() - (uint32_t)IOThreadId::NUM;
    RunPinnedTaskLoopTask runPinnedTaskLoopTasks[ (uint32_t)IOThreadId::NUM ];

    for( uint32_t ioThreadID = 0; ioThreadID < (uint32_t)IOThreadId::NUM; ++ioThreadID )
    {
        runPinnedTaskLoopTasks[ioThreadID].threadNum = ioThreadID + theadNumIOStart;
        g_TS.AddPinnedTask( &runPinnedTaskLoopTasks[ioThreadID] );
    }


    for( g_Run = 0; g_Run< REPEATS; ++g_Run )
    {
        printf("Run %d\n", g_Run.load() );

        // set of a ParallelTaskSet
        // to demonstrate can perform work on enkiTS worker threads but WaitForNewPinnedTasks will
        // not perform work. This can be used to fully subscribe machine with enkiTS worker threads but
        // have extra IO bound threads to handle blocking tasks
        ParallelTaskSet parallelTaskSet;
        g_TS.AddTaskSetToPipe( &parallelTaskSet );

        // Send pretend file IO task to external thread FILE_IO
        PretendDoFileIO pretendDoFileIO;
        pretendDoFileIO.threadNum = (uint32_t)IOThreadId::FILE_IO + theadNumIOStart;
        g_TS.AddPinnedTask( &pretendDoFileIO );

        // Send pretend network IO tasks to external thread  NETWORK_IO_0 ... NUMEXTERNALTHREADS
        PretendDoNetworkIO pretendDoNetworkIO[ (uint32_t)IOThreadId::NUM - (uint32_t)IOThreadId::NETWORK_IO_0 ];
        for( uint32_t ioThreadID = (uint32_t)IOThreadId::NETWORK_IO_0; ioThreadID < (uint32_t)IOThreadId::NUM; ++ioThreadID )
        {
            pretendDoNetworkIO[ioThreadID-(uint32_t)IOThreadId::NETWORK_IO_0].threadNum = ioThreadID + theadNumIOStart;
            g_TS.AddPinnedTask( &pretendDoNetworkIO[ ioThreadID - (uint32_t)IOThreadId::NETWORK_IO_0 ] );
        }

        g_TS.WaitforTask( &parallelTaskSet );

        // in this example  we need to wait for IO tasks to complete before running next loop
        g_TS.WaitforTask( &pretendDoFileIO  );
        for( uint32_t ioThreadID = (uint32_t)IOThreadId::NETWORK_IO_0; ioThreadID < (uint32_t)IOThreadId::NUM; ++ioThreadID )
        {
            g_TS.WaitforTask( &pretendDoNetworkIO[ ioThreadID - (uint32_t)IOThreadId::NETWORK_IO_0 ] );
        }
    }

    // ensure runPinnedTaskLoopTasks complete by explicitly calling WaitforAllAndShutdown
    g_TS.WaitforAllAndShutdown();

    return 0;
}
