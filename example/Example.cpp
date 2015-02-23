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
		uint64_t	count;
		char		cacheline[64];
	};
	Count*    m_pPartialSums;
	uint32_t  m_NumPartialSums;

	ParallelSumTaskSet( uint32_t size_ ) : m_pPartialSums(NULL), m_NumPartialSums(0) { m_SetSize = size_; }
	virtual ~ParallelSumTaskSet()
	{
		delete[] m_pPartialSums;
	}

	void Init()
	{
		delete[] m_pPartialSums;
		m_NumPartialSums = g_TS.GetNumTaskThreads();
		m_pPartialSums = new Count[ m_NumPartialSums ];
		memset( m_pPartialSums, 0, sizeof(Count)*m_NumPartialSums );
	}

	virtual void    ExecuteRange( TaskSetPartition range, uint32_t threadnum )
	{
		assert( m_pPartialSums && m_NumPartialSums );
		uint64_t sum = m_pPartialSums[threadnum].count;
		for( uint64_t i = range.start; i < range.end; ++i )
		{
			sum += i + 1;
		}
		m_pPartialSums[threadnum].count = sum;
	}
  
};

struct ParallelReductionSumTaskSet : ITaskSet
{
	ParallelSumTaskSet m_ParallelSumTaskSet;
	uint64_t m_FinalSum;

	ParallelReductionSumTaskSet( uint32_t size_ ) : m_ParallelSumTaskSet( size_ ), m_FinalSum(0) {
			m_ParallelSumTaskSet.Init();
		}

	virtual void    ExecuteRange( TaskSetPartition range, uint32_t threadnum )
	{
		g_TS.AddTaskSetToPipe( &m_ParallelSumTaskSet );
		g_TS.WaitforTaskSet( &m_ParallelSumTaskSet );

		for( uint32_t i = 0; i < m_ParallelSumTaskSet.m_NumPartialSums; ++i )
		{
			m_FinalSum += m_ParallelSumTaskSet.m_pPartialSums[i].count;
		}
	}
};

static const int WARMUPS	= 10;
static const int RUNS		= 10;
static const int REPEATS	= RUNS + WARMUPS;

int main(int argc, const char * argv[])
{
	uint32_t maxThreads = GetNumHardwareThreads();
	double* avSpeedUps = new double[ maxThreads ];

	for( uint32_t numThreads = 1; numThreads <= maxThreads; ++numThreads )
	{
		g_TS.Initialize(numThreads);
		double avSpeedUp = 0.0;
		for( int run = 0; run< REPEATS; ++run )
		{

			printf("Run %d.....\n", run);
			Timer tParallel;
			tParallel.Start();

			ParallelReductionSumTaskSet m_ParallelReductionSumTaskSet( 10 * 1024 * 1024 );

			g_TS.AddTaskSetToPipe( &m_ParallelReductionSumTaskSet );

			g_TS.WaitforTaskSet( &m_ParallelReductionSumTaskSet );

			tParallel.Stop();


			printf("Parallel Example complete in \t%fms,\t sum: %" PRIu64 "\n", tParallel.GetTimeMS(), m_ParallelReductionSumTaskSet.m_FinalSum );

			Timer tSerial;
			tSerial.Start();
			uint64_t sum = 0;
			for( uint64_t i = 0; i < (uint64_t)m_ParallelReductionSumTaskSet.m_ParallelSumTaskSet.m_SetSize; ++i )
			{
				sum += i + 1;
			}

			tSerial.Stop();

			if( run >= WARMUPS )
			{
				avSpeedUp += tSerial.GetTimeMS()  / tParallel.GetTimeMS() / RUNS;
			}

			printf("Serial Example complete in \t%fms,\t sum: %" PRIu64 "\n", tSerial.GetTimeMS(), sum );
			printf("Speed Up Serial / Parallel: %f\n\n", tSerial.GetTimeMS()  / tParallel.GetTimeMS() );

		}
		avSpeedUps[numThreads-1] = avSpeedUp;
		printf("\nAverage Speed Up for %d Hardware Threads Serial / Parallel: %f\n", numThreads, avSpeedUp );
	}

	printf("\nHardware Threads, Av Speed Up/s\n" );
	for( uint32_t numThreads = 1; numThreads <= maxThreads; ++numThreads )
	{
		printf("%d, %f\n", numThreads, avSpeedUps[numThreads-1] );
	}

	return 0;
}
