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
#include "Timer.h"

#include <stdio.h>

enki::TaskScheduler g_TS;
uint32_t g_Iteration;

struct SlowTask : enki::ITaskSet
{
    virtual void ExecuteRange( enki::TaskSetPartition range, uint32_t threadnum )
    {
        // fake slow task with timer
        Timer timer;
        timer.Start();
        double tWaittime = (double)( range.end - range.start ) * waitTimeMultiplier;
        while( timer.GetTimeMS() < tWaittime )
        {
        }
        //printf( "\t SlowTask range complete: thread: %d, start: %d, end: %d\n",
        //        threadnum, range.start, range.end );
    }

    double waitTimeMultiplier;
};

struct WaitingTask : enki::ITaskSet
{
    virtual void ExecuteRange( enki::TaskSetPartition range, uint32_t threadnum )
    {
        int numWaitTasks = maxWaitasks - depth;
        for( int t = 0; t < numWaitTasks; ++t )
        {
            pWaitingTasks[t] = new WaitingTask;
            pWaitingTasks[t]->depth = depth + 1;
            g_TS.AddTaskSetToPipe( pWaitingTasks[t] );
        }
        for( SlowTask& task : tasks )
        {
            task.m_SetSize = rand() % 1000;
            task.waitTimeMultiplier = 0.0001 * double( rand() % 100 );
            g_TS.AddTaskSetToPipe( &task );
        }
        for( SlowTask& task : tasks )
        {
            g_TS.WaitforTask( &task );
        }

        for( int t = 0; t < numWaitTasks; ++t )
        {
            g_TS.WaitforTask( pWaitingTasks[t] );
        }
        printf( "\tIteration %d: WaitingTask depth %d complete: thread: %d\n", g_Iteration, depth, threadnum );
    }

    virtual ~WaitingTask()
    {
        for( WaitingTask* pWaitingTask : pWaitingTasks )
        {
            delete pWaitingTask;
        }
    }
    SlowTask    tasks[10];
    int32_t     depth = 0;
    static constexpr int maxWaitasks = 6;
    WaitingTask* pWaitingTasks[maxWaitasks] = {};
};


// This example demonstrates how to run a long running task alongside tasks
// which must complete as early as possible using priorities.
int main(int argc, const char * argv[])
{
    g_TS.Initialize();
    for( g_Iteration = 0; g_Iteration < 1000; ++g_Iteration )
    {
        WaitingTask taskRoot;
        g_TS.AddTaskSetToPipe( &taskRoot );
        g_TS.WaitforAll();
    }
    return 0;
}
