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

enkiTaskScheduler*    pETS;
enkiTaskSet*        pParallelTask;
enkiPinnedTask*     pPinnedTask;

static const int REPEATS = 5;

void PinnedTaskFunc( void* pArgs_ )
{
    printf("This will run on the main thread\n");
}

void ParallelTaskSetFunc( uint32_t start_, uint32_t end, uint32_t threadnum_, void* pArgs_ )
{
    enkiAddPinnedTask( pETS, pPinnedTask );
    printf("This could run on any thread, currently thread %d\n", threadnum_);
    enkiWaitForPinnedTask( pETS, pPinnedTask );
}

int main(int argc, const char * argv[])
{
    int run;
    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    pParallelTask = enkiCreateTaskSet( pETS, ParallelTaskSetFunc );
    pPinnedTask = enkiCreatePinnedTask( pETS, PinnedTaskFunc, 0 ); // pinned task is created for thread 0


    for( run = 0; run< REPEATS; ++run )
    {
        enkiAddTaskSet( pETS, pParallelTask );

        enkiRunPinnedTasks( pETS );
    
        enkiWaitForTaskSet( pETS, pParallelTask );
    }

    enkiDeleteTaskSet( pETS, pParallelTask );
    enkiDeletePinnedTask( pETS, pPinnedTask );

    enkiDeleteTaskScheduler( pETS );
}
