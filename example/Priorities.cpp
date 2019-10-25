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

struct ExampleTask : enki::ITaskSet
{
    ExampleTask( uint32_t size_ ) { m_SetSize = size_; }

    virtual void ExecuteRange( enki::TaskSetPartition range_, uint32_t threadnum_ )
    {
        if( m_Priority == enki::TASK_PRIORITY_LOW )
        {
            // fake slow task with timer
            Timer timer;
            timer.Start();
            double tWaittime = (double)( range_.end - range_.start ) * 100.;
            while( timer.GetTimeMS() < tWaittime )
            {
            }
            printf( "\tLOW PRIORITY TASK range complete: thread: %d, start: %d, end: %d\n",
                    threadnum_, range_.start, range_.end );
        }
        else
        {
            printf( "HIGH PRIORITY TASK range complete: thread: %d, start: %d, end: %d\n",
                    threadnum_, range_.start, range_.end );
        }
    }
};


// This example demonstrates how to run a long running task alongside tasks
// which must complete as early as possible using priorities.
int main(int argc, const char * argv[])
{
    g_TS.Initialize();

    ExampleTask lowPriorityTask( 10 );
    lowPriorityTask.m_Priority  = enki::TASK_PRIORITY_LOW;

    ExampleTask highPriorityTask( 1 );
    highPriorityTask.m_Priority = enki::TASK_PRIORITY_HIGH;

    g_TS.AddTaskSetToPipe( &lowPriorityTask );
    for( int task = 0; task < 10; ++task )
    {
        // run high priority tasks
        g_TS.AddTaskSetToPipe( &highPriorityTask );

        // wait for task but only run tasks of the same priority on this thread
        g_TS.WaitforTask( &highPriorityTask, highPriorityTask.m_Priority );
    }
    // wait for low priority task, run any tasks on this thread whilst waiting
    g_TS.WaitforTask( &lowPriorityTask );

    return 0;
}
