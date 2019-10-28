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

#include "TaskScheduler.h"
#include "Timer.h"

#include <stdio.h>
#include <inttypes.h>
#include <math.h>

#ifndef _WIN32
    #include <string.h>
#endif

using namespace enki;


const uint32_t numTasks =  1024*1024;

TaskScheduler g_TS;


struct ConsumeTask : ITaskSet
{
    static ConsumeTask tasks[numTasks];

    struct Count
    {
        // prevent false sharing.
        uint32_t    count;
        char        cacheline[64];
    };
    static Count*   pCount;
    static uint32_t numCount;

    virtual void    ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ )
    {
        ++pCount[threadnum_].count;
    }

    static void Init()
    {
        delete[] ConsumeTask::pCount;
        numCount = g_TS.GetNumTaskThreads();
        ConsumeTask::pCount = new Count[ numCount ];
        memset( pCount, 0, sizeof(Count) * numCount );
    }
};

ConsumeTask              ConsumeTask::tasks[numTasks];
ConsumeTask::Count*      ConsumeTask::pCount = NULL;
uint32_t                 ConsumeTask::numCount = 0;



struct CreateTasks : ITaskSet
{
    CreateTasks()
    {
        m_SetSize = numTasks;
    }
    virtual void    ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ )
    {
        for( uint32_t i=range_.start; i <range_.end; ++i )
        {
            ConsumeTask& task = ConsumeTask::tasks[ i ];
            g_TS.AddTaskSetToPipe( &task );
        }
    }
};



static const int WARMUPS    = 5;
static const int RUNS        = 10;
static const int REPEATS    = RUNS + WARMUPS;

int main(int argc, const char * argv[])
{
    uint32_t maxThreads = enki::GetNumHardwareThreads();
    double* times = new double[ maxThreads ];
    double* stdev = new double[ maxThreads ];


    for( uint32_t numThreads = 1; numThreads <= maxThreads; ++numThreads )
    {
        g_TS.Initialize(numThreads);

        double avTime = 0.0;
        double avTime2 = 0.0;
        uint32_t totalErrors = 0;
        for( int run = 0; run< REPEATS; ++run )
        {

            printf("Run %d.....\n", run);
            Timer tParallel;
            CreateTasks createTask;
            ConsumeTask::Init();

            tParallel.Start();


            g_TS.AddTaskSetToPipe( &createTask );

            g_TS.WaitforAll();

            tParallel.Stop();


            printf("Parallel Example complete in \t%fms, task rate: %f M tasks/s\n", tParallel.GetTimeMS(), numTasks / tParallel.GetTimeMS() / 1000.0f );

            printf("Parallel Example error checking...");
            uint32_t numTasksDone = 0;
            for( uint32_t check = 0; check < ConsumeTask::numCount; ++check )
            {
                numTasksDone += ConsumeTask::pCount[check].count;
            }
            if( numTasksDone != numTasks )
            {
                printf("\n ERRORS FOUND - %d tasks not done!!!\n", numTasks - numTasksDone );
            }
            else
            {
                printf(" no errors found.\n" );
            }

            if( run >= WARMUPS )
            {
                avTime2 += tParallel.GetTimeMS() * tParallel.GetTimeMS();
                avTime += tParallel.GetTimeMS() / RUNS;
            }
        }

        printf("\nAverage Time for %d Hardware Threads: %fms, rate: %f M tasks/s. %d errors found.\n", numThreads, avTime, numTasks / avTime / 1000.0f, totalErrors );

        times[numThreads-1] = avTime;
        stdev[numThreads-1] = sqrt(RUNS * avTime2 - (RUNS * avTime)*(RUNS * avTime)) / RUNS;
    }

    printf("\nHardware Threads, Time, std, MTasks/s, Perf Multiplier\n" );
    for( uint32_t numThreads = 1; numThreads <= maxThreads; ++numThreads )
    {
        printf("%d, %f, %f, %f, %f\n", numThreads, times[numThreads-1], stdev[numThreads-1], numTasks / times[numThreads-1] / 1000.0f, times[0] / times[numThreads-1] );
    }


    delete[] times;

    return 0;
}
