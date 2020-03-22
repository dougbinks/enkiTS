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

struct CompletionArgs
{
    enkiTaskSet*          pTask;
    enkiCompletionAction* pCompletionAction;
};

void CompletionFunction( void* pArgs_, uint32_t threadNum_ )
{
    struct CompletionArgs* pCompletionArgs = pArgs_;
    struct enkiParamsTaskSet params = enkiGetParamsTaskSet( pCompletionArgs->pTask );
    uint32_t* pTaskNum = params.pArgs;
    printf("CompletionFunction for task %u running on thread %u\n", *pTaskNum, threadNum_ );
    enkiDeleteCompletionAction( pETS, pCompletionArgs->pCompletionAction );
    enkiDeleteTaskSet( pETS, pCompletionArgs->pTask );
    free( pTaskNum );
    free( pCompletionArgs );
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
    struct enkiParamsCompletionAction paramsCompletionAction;
    uint32_t* pTaskNum;
    struct CompletionArgs* pCompletionArgs;

    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    // Here we demonstrate using the completion action to delete the tasks on the fly
    for( run=0; run<10; ++run )
    {
        pCompletionArgs = malloc(sizeof(struct CompletionArgs)); // we will free in CompletionFunction
        pCompletionArgs->pTask             = enkiCreateTaskSet( pETS, TaskSetFunc );
        pTaskNum = malloc(sizeof(uint32_t)); // we will free in CompletionFunction
        *pTaskNum = run;
        enkiSetArgsTaskSet( pCompletionArgs->pTask, pTaskNum );
        pCompletionArgs->pCompletionAction = enkiCreateCompletionAction( pETS, CompletionFunction );
        paramsCompletionAction = enkiGetParamsCompletionAction( pCompletionArgs->pCompletionAction );
        paramsCompletionAction.pArgs       = pCompletionArgs;
        paramsCompletionAction.pDependency = enkiGetCompletableFromTaskSet( pCompletionArgs->pTask );
        enkiSetParamsCompletionAction( pCompletionArgs->pCompletionAction, paramsCompletionAction );

        enkiAddTaskSet( pETS, pCompletionArgs->pTask );
    }
    enkiWaitForAll( pETS );

    enkiDeleteTaskScheduler( pETS );
}
