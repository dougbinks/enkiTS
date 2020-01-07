// Copyright (c) 2020 Doug Binks
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
#include <assert.h>

#ifndef _WIN32
    #include <string.h>
#endif

using namespace enki;





TaskScheduler g_TS;
uint32_t      g_numTestsRun       = 0;
uint32_t      g_numTestsSucceeded = 0;

void RunTestFunction(  const char* pTestFuncName_, std::function<bool ()> TestFunc )
{
    bool bSuccess = TestFunc();
    ++g_numTestsRun;
    if( bSuccess )
    {
        fprintf(stdout, "SUCCESS: Test %2u: %s.\n", g_numTestsRun, pTestFuncName_ );
        ++g_numTestsSucceeded;
    }
    else
    {
        fprintf(stderr, "FAILURE: Test %2u: %s.\n", g_numTestsRun, pTestFuncName_ );
    }
}

struct ParallelSumTaskSet : ITaskSet
{
    struct Count
    {
        // prevent false sharing.
        uint64_t    count;
        char        cacheline[64];
    };
    Count*    m_pPartialSums;
    uint32_t  m_NumPartialSums;

    ParallelSumTaskSet( uint32_t size_ ) : m_pPartialSums(NULL), m_NumPartialSums(0) { m_SetSize = size_; }
    virtual ~ParallelSumTaskSet()
    {
        delete[] m_pPartialSums;
    }

    void Init( uint32_t numPartialSums_ )
    {
        delete[] m_pPartialSums;
        m_NumPartialSums =numPartialSums_ ;
        m_pPartialSums = new Count[ m_NumPartialSums ];
        memset( m_pPartialSums, 0, sizeof(Count)*m_NumPartialSums );
    }

    virtual void    ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ )
    {
        assert( m_pPartialSums && m_NumPartialSums );
        uint64_t sum = m_pPartialSums[threadnum_].count;
        for( uint64_t i = range_.start; i < range_.end; ++i )
        {
            sum += i + 1;
        }
        m_pPartialSums[threadnum_].count = sum;
    }
  
};

struct ParallelReductionSumTaskSet : ITaskSet
{
    ParallelSumTaskSet m_ParallelSumTaskSet;
    uint64_t m_FinalSum;

    ParallelReductionSumTaskSet( uint32_t size_ ) : m_ParallelSumTaskSet( size_ ), m_FinalSum(0)
    {
            m_ParallelSumTaskSet.Init( g_TS.GetNumTaskThreads() );
    }

    virtual void    ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ )
    {
        g_TS.AddTaskSetToPipe( &m_ParallelSumTaskSet );
        g_TS.WaitforTask( &m_ParallelSumTaskSet );

        for( uint32_t i = 0; i < m_ParallelSumTaskSet.m_NumPartialSums; ++i )
        {
            m_FinalSum += m_ParallelSumTaskSet.m_pPartialSums[i].count;
        }
    }
};


// Example thread function
// May want to use threads for blocking IO, during which enkiTS task threads can do work
void threadFunction( uint32_t setSize_, bool* pbRegistered_, uint64_t* pSumParallel_ )
{
    *pbRegistered_ = g_TS.RegisterExternalTaskThread();
    if( *pbRegistered_ )
    {
        ParallelReductionSumTaskSet task( setSize_ );
        g_TS.AddTaskSetToPipe( &task );
        g_TS.WaitforTask( &task);
        g_TS.DeRegisterExternalTaskThread();
        *pSumParallel_ = task.m_FinalSum;
    }
}

struct PinnedTask : IPinnedTask
{
    PinnedTask()
        : IPinnedTask( enki::GetNumHardwareThreads() - 1 ) // set pinned thread to 0
    {}
    virtual void Execute()
    {
        threadRunOn = g_TS.GetThreadNum();
    }
    uint32_t threadRunOn = 0;
};



int main(int argc, const char * argv[])
{
    fprintf( stdout,"\n---Running Tests----\n" );

    uint32_t setSize = 20 * 1024 * 1024;
    uint64_t sumSerial;

    // evaluate serial for test comparison with parallel runs
    ParallelSumTaskSet serialTask( setSize );
    serialTask.Init( 1 );
    TaskSetPartition range = { 0, setSize };
    serialTask.ExecuteRange( range, 0 );
    sumSerial = serialTask.m_pPartialSums[0].count;

    RunTestFunction(
        "Parallel Reduction Sum",
        [&]()->bool
        {
            g_TS.Initialize();
            ParallelReductionSumTaskSet parallelReductionSumTaskSet( setSize );
            g_TS.AddTaskSetToPipe( &parallelReductionSumTaskSet );
            g_TS.WaitforTask( &parallelReductionSumTaskSet );
            fprintf( stdout,"\tParallelReductionSum: %" PRIu64 ", sumSerial: %" PRIu64 "\n", parallelReductionSumTaskSet.m_FinalSum, sumSerial );
            return parallelReductionSumTaskSet.m_FinalSum == sumSerial;
        } );

    RunTestFunction(
        "External Thread",
        [&]()->bool
        {
            enki::TaskSchedulerConfig config;
            config.numExternalTaskThreads = 1;
            bool bRegistered = false;
            uint64_t sumParallel = 0;
            g_TS.Initialize( config );

            std::thread threads( threadFunction, setSize, &bRegistered, &sumParallel );
            threads.join();
            fprintf( stdout,"\tExternal thread sum: %" PRIu64 ", sumSerial: %" PRIu64 "\n", sumParallel, sumSerial );
            if( !bRegistered )
            {
                fprintf( stderr,"\tExternal thread did not register\n" );
                return false;
            }
            if( sumParallel != sumSerial )
            {
                return false;
            }
            return true;
        } );

    RunTestFunction(
        "Pinned Task",
        [&]()->bool
        {
            g_TS.Initialize();
            PinnedTask pinnedTask;
            g_TS.AddPinnedTask( &pinnedTask );
            g_TS.WaitforTask( &pinnedTask );
            fprintf( stdout,"\tPinned task ran on thread %u, requested thread %u\n", pinnedTask.threadRunOn, pinnedTask.threadNum );
            return pinnedTask.threadRunOn == pinnedTask.threadNum;
        } );


    fprintf( stdout, "\n%u Tests Run\n%u Tests Succeeded\n\n", g_numTestsRun, g_numTestsSucceeded );
    if( g_numTestsRun == g_numTestsSucceeded )
    {
        fprintf( stdout, "All tests SUCCEEDED\n" );
    }
    else
    {
        fprintf( stderr, "%u tests FAILED\n", g_numTestsRun - g_numTestsSucceeded );
        return 1;
    }
    return 0;
}
