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
#include <chrono>

#include <stdio.h>
#include <inttypes.h>

#ifndef _WIN32
	#include <string.h>
#endif

using namespace enki;
using namespace std::chrono;




TaskScheduler g_TS;

struct ParallelSumTaskSet : ITaskSet
{
	uint64_t* m_pPartialSums;
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
		m_pPartialSums = new uint64_t[ m_NumPartialSums ];
		memset( m_pPartialSums, 0, sizeof(uint64_t)*m_NumPartialSums );
	}

	virtual void    ExecuteRange( TaskSetPartition range, uint32_t threadnum )
	{
		assert( m_pPartialSums && m_NumPartialSums );
		uint64_t sum = m_pPartialSums[threadnum];
		for( uint64_t i = range.start; i < range.end; ++i )
		{
			sum += i + 1;
		}
		m_pPartialSums[threadnum] = sum;
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
			m_FinalSum += m_ParallelSumTaskSet.m_pPartialSums[i];
		}
	}
};

static const int WARMUPS	= 10;
static const int RUNS		= 10;
static const int REPEATS	= RUNS + WARMUPS;

int main(int argc, const char * argv[])
{
	g_TS.Initialize();

	double avSpeedUp = 0.0;
	for( int i = 0; i < REPEATS; ++i )
	{

		printf("Run %d.....\n", i);
		auto tStartParallel = high_resolution_clock::now();

		ParallelReductionSumTaskSet m_ParallelReductionSumTaskSet( 10 * 1024 * 1024 );

		g_TS.AddTaskSetToPipe( &m_ParallelReductionSumTaskSet );

		g_TS.WaitforTaskSet( &m_ParallelReductionSumTaskSet );

		auto tEndParallel = high_resolution_clock::now();

		double timeTakenParallel = duration_cast<duration<double>>(tEndParallel - tStartParallel).count();

		printf("Parallel Example complete in \t%fms,\t sum: %" PRIu64 "\n", 1000.0 * timeTakenParallel, m_ParallelReductionSumTaskSet.m_FinalSum );

		auto tStartSerial = high_resolution_clock::now();
		uint64_t sum = 0;
		for( uint64_t i = 0; i < (uint64_t)m_ParallelReductionSumTaskSet.m_ParallelSumTaskSet.m_SetSize; ++i )
		{
			sum += i + 1;
		}

		auto tEndSerial= high_resolution_clock::now();
		double timeTakenSerial= duration_cast<duration<double>>(tEndSerial - tStartSerial).count();

		if( i < WARMUPS )
		{
			avSpeedUp += timeTakenSerial  / timeTakenParallel / RUNS;
		}

		printf("Serial Example complete in \t%fms,\t sum: %" PRIu64 "\n", 1000.0 * timeTakenSerial, sum );
		printf("Speed Up Serial / Parallel: %f\n\n", timeTakenSerial  / timeTakenParallel );

	}

	printf("\nAverage Speed Up for %d Hardware Threads Serial / Parallel: %f\n", GetNumHardwareThreads(), avSpeedUp );


	return 0;
}
