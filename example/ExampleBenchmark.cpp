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




const uint32_t numTasks =  1024*1024;

TaskScheduler g_TS;



struct SplitTask : ITaskSet
{
	static SplitTask tasks[numTasks];
	uint32_t taskId;
	virtual void    ExecuteRange( TaskSetPartition range, uint32_t threadnum )
	{
		uint32_t num = range.end - range.start - 1;
		if( num  )
		{
			uint32_t newTaskId = taskId + range.start + 1;
			assert( taskId < numTasks );
			SplitTask& task = tasks[ newTaskId ];
			task.taskId = newTaskId;
			task.m_SetSize = num;
			g_TS.AddTaskSetToPipe( &task );
		}

	}
};

SplitTask SplitTask::tasks[numTasks];


static const int WARMUPS	= 10;
static const int RUNS		= 20;
static const int REPEATS	= RUNS + WARMUPS;

int main(int argc, const char * argv[])
{
	g_TS.Initialize();

	double avTime = 0.0;
	for( int run = 0; run< REPEATS; ++run )
	{

		printf("Run %d.....\n", run);
		Timer tParallel;
		tParallel.Start();

		SplitTask::tasks[0].taskId = 0;
		SplitTask::tasks[0].m_SetSize = numTasks;

		g_TS.AddTaskSetToPipe( &SplitTask::tasks[0] );

		g_TS.WaitforAll();

		tParallel.Stop();


		printf("Parallel Example complete in \t%fms, task rate: %f M tasks/s\n", tParallel.GetTimeMS(), numTasks / tParallel.GetTimeMS() / 1000.0f );


		if( run >= WARMUPS )
		{
			avTime += tParallel.GetTimeMS() / RUNS;
		}
	}

	printf("\nAverage Time for %d Hardware Threads: %fms, rate: %f M tasks/s\n", GetNumHardwareThreads(), avTime, numTasks / avTime / 1000.0f );


	return 0;
}
