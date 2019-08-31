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
        if( depth < 4 )
        {
            pWaitingTask0 = new WaitingTask;
            pWaitingTask0->depth = depth + 1;
            g_TS.AddTaskSetToPipe( pWaitingTask0 );
            pWaitingTask1 = new WaitingTask;
            pWaitingTask1->depth = depth + 1;
            g_TS.AddTaskSetToPipe( pWaitingTask1 );
        }
        for( SlowTask& task : tasks )
        {
            task.m_SetSize = 10;
            task.waitTimeMultiplier = 10.;
            g_TS.AddTaskSetToPipe( &task );
        }
        for( SlowTask& task : tasks )
        {
            g_TS.WaitforTask( &task );
        }

        if( pWaitingTask0 )
        {
            g_TS.WaitforTask( pWaitingTask0 );
            g_TS.WaitforTask( pWaitingTask1 );
        }
        printf( "\t WaitingTask depth %d complete: thread: %d\n", depth, threadnum );
    }

    virtual ~WaitingTask()
    {
        delete pWaitingTask0;
        delete pWaitingTask1;
    }

    SlowTask    tasks[10];
    int32_t     depth = 0;
    WaitingTask* pWaitingTask0 = NULL;
    WaitingTask* pWaitingTask1 = NULL;
};


// This example demonstrates how to run a long running task alongside tasks
// which must complete as early as possible using priorities.
int main(int argc, const char * argv[])
{
    g_TS.Initialize();

    for( int i = 0; i < 100; ++i )
    {
        WaitingTask taskRoot;
        g_TS.AddTaskSetToPipe( &taskRoot );
        g_TS.WaitforTask( &taskRoot );
    }
    return 0;
}
