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

#include "TaskScheduler_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enkiTaskScheduler*    pETS;

void CompletionFunction( void* pArgs_, uint32_t threadNum_ )
{
    enkiTaskSet* pTaskSet = pArgs_;
    struct enkiParamsTaskSet params = enkiGetParamsTaskSet( pTaskSet );
    uint32_t* pTaskNum = params.pArgs;
    printf("CompletionFunction for task %u running on thread %u\n", *pTaskNum, threadNum_ );
    free( pTaskNum );
    enkiDeleteTaskSet( pETS, pTaskSet );
}

void TaskSetFunc( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    (void)start_; (void)end_;
    uint32_t* pTaskNum = pArgs_;
    printf("Task %u running on thread %u\n", *pTaskNum, threadnum_);
}

int main(int argc, const char * argv[])
{
    int run;
    enkiTaskSet*          pTask; // pCompletionAction will delete pTask
    enkiCompletionAction* pCompletionAction;
    struct enkiParamsCompletionAction paramsCompletionAction;
    uint32_t* pTaskNum;

    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    // Here we demonstrate using the completion action to delete the tasks on the fly
    for( run=0; run<10; ++run )
    {
        pTask             = enkiCreateTaskSet( pETS, TaskSetFunc );
        pTaskNum = malloc(sizeof(uint32_t)); // we will free in CompletionFunction
        *pTaskNum = run;
        enkiSetArgsTaskSet( pTask, pTaskNum );
        pCompletionAction = enkiCreateCompletionAction( pETS, CompletionFunction );
        paramsCompletionAction = enkiGetParamsCompletionAction( pCompletionAction );
        paramsCompletionAction.pArgs       = pTask; // we will use pArgs to delete pTask
        paramsCompletionAction.pDependency = enkiGetCompletableFromTaskSet( pTask );
        enkiSetParamsCompletionAction( pCompletionAction, paramsCompletionAction );

        enkiAddTaskSet( pETS, pTask );
    }
    enkiWaitForAll( pETS );

    enkiDeleteTaskScheduler( pETS );
}