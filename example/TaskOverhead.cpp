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
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#undef __STDC_FORMAT_MACROS
#include <assert.h>
#include <string.h>

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

    ParallelSumTaskSet( uint32_t size_ ) : m_pPartialSums(NULL), m_NumPartialSums(0) { m_SetSize = size_; m_MinRange = 1024; }
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

    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
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
    ParallelSumTaskSet* m_pParallelSum;
    Dependency          m_Dependency;
    uint64_t            m_FinalSum;

    ParallelReductionSumTaskSet( ParallelSumTaskSet* pParallelSum_ ) : m_pParallelSum( pParallelSum_ ), m_Dependency( pParallelSum_, this ), m_FinalSum(0)
    {
    }

    void ExecuteRange( TaskSetPartition range, uint32_t threadnum ) override
    {
        for( uint32_t i = 0; i < m_pParallelSum->m_NumPartialSums; ++i )
        {
            m_FinalSum += m_pParallelSum->m_pPartialSums[i].count;
        }
    }
};

static const int WARMUPS     = 10;
static const int RUNS        = 100;
static const int REPEATS     = RUNS + WARMUPS;

static const int DATA_POINTS = 50;

uint32_t GetSetSizeForDataPoint( uint32_t dataPoint_ )
{
    dataPoint_++;
    return dataPoint_ * dataPoint_ * 1024;
}

int main(int argc, const char * argv[])
{
    double* avTimeTakenMS = new double[ DATA_POINTS ];
    double* avSerialTimeTakenMS = new double[ DATA_POINTS ];

        
    g_TS.Initialize();
    for( uint32_t dataPoint = 0; dataPoint < DATA_POINTS; ++dataPoint )
    {
        double avTimeMS = 0.0;
        uint32_t setSize = GetSetSizeForDataPoint( dataPoint );

        double avSerialTimeMS = 0.0f;
        uint64_t sumSerial;
        for( int run = 0; run< REPEATS; ++run )
        {
            printf("Run %d.....\n", run);

            // start by measuring serial
            ParallelSumTaskSet serialTask( setSize );
            serialTask.Init( 1 );
            TaskSetPartition range = { 0, setSize };

            Timer tSerial;
            tSerial.Start();

            serialTask.ExecuteRange( range, 0 );
            sumSerial = serialTask.m_pPartialSums[0].count;

            tSerial.Stop();
            printf("Serial Example complete in \t%fms,\t sum: %" PRIu64 "\n", tSerial.GetTimeMS(), sumSerial );


            // Now measure parallel
            ParallelSumTaskSet          parallelSumTask( setSize );
            parallelSumTask.Init( g_TS.GetNumTaskThreads() );
            ParallelReductionSumTaskSet parallelReductionSumTaskSet( &parallelSumTask );

            Timer tParallel;
            tParallel.Start();

            g_TS.AddTaskSetToPipe( &parallelSumTask );

            g_TS.WaitforTask( &parallelReductionSumTaskSet );

            tParallel.Stop();
            printf("Parallel Example complete in \t%fms,\t sum: %" PRIu64 "\n", tParallel.GetTimeMS(), parallelReductionSumTaskSet.m_FinalSum );

            if( sumSerial != parallelReductionSumTaskSet.m_FinalSum )
            {
                printf("ERROR, sums do not match\n");
                return -1;
            }

            if( run >= WARMUPS )
            {
                avSerialTimeMS += tSerial.GetTimeMS() / RUNS;
                avTimeMS += tParallel.GetTimeMS() / RUNS;
            }
        }
        avSerialTimeTakenMS[ dataPoint ] = avSerialTimeMS;
        avTimeTakenMS[ dataPoint ]       = avTimeMS;
        printf("\nAverage time for set size %d: %fms parallel, %fms serial\n", setSize, (float)avTimeMS, (float)avSerialTimeMS );
    }

    printf("\nSet Size,\tTime Parallel/ms,\tTime Serial/ms\n" );
    for( uint32_t dataPoint = 0; dataPoint < DATA_POINTS; ++dataPoint )
    {
        printf("%8d,\t%f,\t%f\n", GetSetSizeForDataPoint( dataPoint ), (float)avTimeTakenMS[ dataPoint ], (float)avSerialTimeTakenMS[ dataPoint ] );
    }

    return 0;
}
