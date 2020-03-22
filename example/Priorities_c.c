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

#include "TaskScheduler_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>


void LowPriorityTask( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    int wait = (int)( 0.1f * (float)( ( end_ - start_ ) * (float)CLOCKS_PER_SEC) );
    clock_t endTime = (clock_t)wait  + clock();
    while( clock() < endTime )
    {
    }
    printf( "\tLOW PRIORITY TASK range complete: thread: %d, start: %d, end: %d\n",
                    threadnum_, start_, end_ );
}

void HighPriorityTask( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    printf( "HIGH PRIORITY TASK range complete: thread: %d, start: %d, end: %d\n",
                    threadnum_, start_, end_ );
}

int main(int argc, const char * argv[])
{
    int run;
    enkiTaskScheduler*    pETS;
    enkiTaskSet*          plowPriorityTask;
    enkiTaskSet*          pHighPriorityTask;

    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    plowPriorityTask = enkiCreateTaskSet( pETS, LowPriorityTask );
    enkiSetPriorityTaskSet( plowPriorityTask, 1 ); // lower values higher priority
    enkiSetSetSizeTaskSet( plowPriorityTask, 10 );

    pHighPriorityTask = enkiCreateTaskSet( pETS, HighPriorityTask );
    enkiSetPriorityTaskSet( pHighPriorityTask, 0 ); // lower values higher priority

    enkiAddTaskSet( pETS, plowPriorityTask );
    for( run = 0; run < 10; ++run )
    {
        // run high priority tasks
        enkiAddTaskSet( pETS, pHighPriorityTask );

        // wait for task but only run tasks of the same priority or higher on this thread
        enkiWaitForTaskSetPriority( pETS, pHighPriorityTask, 0 );
    }

    // wait for low priority task, run any tasks on this thread whilst waiting
    enkiWaitForTaskSet( pETS, plowPriorityTask );

    enkiDeleteTaskSet( pETS, plowPriorityTask );
    enkiDeleteTaskSet( pETS, pHighPriorityTask );

    enkiDeleteTaskScheduler( pETS );
}
