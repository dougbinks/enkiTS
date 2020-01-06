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


int main(int argc, const char * argv[])
{
    uint32_t setSize = 20 * 1024 * 1024;
    uint64_t sumSerial;

    // evaluate serial
    ParallelSumTaskSet serialTask( setSize );
    serialTask.Init( 1 );
    TaskSetPartition range = { 0, setSize };
    serialTask.ExecuteRange( range, 0 );
    sumSerial = serialTask.m_pPartialSums[0].count;


    // now test parallel
    g_TS.Initialize();
    ParallelReductionSumTaskSet parallelReductionSumTaskSet( setSize );
    g_TS.AddTaskSetToPipe( &parallelReductionSumTaskSet );
    g_TS.WaitforTask( &parallelReductionSumTaskSet );
    if( parallelReductionSumTaskSet.m_FinalSum != sumSerial )
    {
        fprintf( stderr,"parallelReductionSumTaskSet.m_FinalSum: %" PRIu64 " != sumSerial: %" PRIu64 "\n", parallelReductionSumTaskSet.m_FinalSum, sumSerial );
        return -1;
    }

    // now test parallel with external threads
    enki::TaskSchedulerConfig config;
    config.numExternalTaskThreads = 1;
    bool bRegistered = false;
    uint64_t sumParallel = 0;
    g_TS.Initialize( config );

    std::thread threads( threadFunction, setSize, &bRegistered, &sumParallel );
    threads.join();
    if( !bRegistered )
    {
        fprintf( stderr,"External thread did not register\n" );
        return -2;
    }
    if( sumParallel != sumSerial )
    {
        fprintf( stderr,"External thread sum: %" PRIu64 " != sumSerial: %" PRIu64 "\n", sumParallel, sumSerial );
        return -3;
    }

    fprintf( stdout, "All tests succeeded\n" );
    return 0;
}
