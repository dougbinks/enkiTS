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

struct CompletionArgs_ModifyTask
{
    enkiTaskSet*          pTaskB;
    uint32_t              run;
};

struct CompletionArgs_DeleteTask
{
    enkiTaskSet*          pTask;
    enkiDependency*       pDependency;       // in this example only 1 or 0 dependencies, but generally could be an array
    enkiCompletionAction* pCompletionAction;
    uint32_t              run;               // only required for example output, not needed for a general purpose delete task
};

// In this example all our TaskSet functions share the same args struct, but we could use different one
struct TaskSetArgs
{
    enkiTaskSet* pTask;
    const char*  name;
    uint32_t     run;
};

void CompletionFunctionPreComplete_ModifyDependentTask( void* pArgs_, uint32_t threadNum_ )
{
    struct CompletionArgs_ModifyTask* pCompletionArgs_ModifyTask  = pArgs_;
    struct enkiParamsTaskSet paramsTaskNext = enkiGetParamsTaskSet( pCompletionArgs_ModifyTask->pTaskB );

    printf("CompletionFunctionPreComplete_ModifyDependentTask for run %u running on thread %u\n",
            pCompletionArgs_ModifyTask->run, threadNum_ );

    // in this function we can modify the parameters of any task which depends on this CompletionFunction
    // pre complete functions should not be used to delete the current CompletionAction, for that use PostComplete functions
    paramsTaskNext.setSize = 10; // modify the set size of the next task - for example this could be based on output from previous task
    enkiSetParamsTaskSet( pCompletionArgs_ModifyTask->pTaskB, paramsTaskNext );

    free( pCompletionArgs_ModifyTask );
}


void CompletionFunctionPostComplete_DeleteTask( void* pArgs_, uint32_t threadNum_ )
{
    struct CompletionArgs_DeleteTask* pCompletionArgs_DeleteTask = pArgs_;

    printf("CompletionFunctionPostComplete_DeleteTask for run %u running on thread %u\n",
           pCompletionArgs_DeleteTask->run, threadNum_ );

    // can free memory in post complete

    // note must delete a dependency before you delete the dependency task and the task to run on completion
    if( pCompletionArgs_DeleteTask->pDependency )
    {
        enkiDeleteDependency( pETS, pCompletionArgs_DeleteTask->pDependency );
    }

    free( enkiGetParamsTaskSet( pCompletionArgs_DeleteTask->pTask ).pArgs );
    enkiDeleteTaskSet( pETS, pCompletionArgs_DeleteTask->pTask );

    enkiDeleteCompletionAction( pETS, pCompletionArgs_DeleteTask->pCompletionAction );

    // safe to free our own args in this example as no other function dereferences them
    free( pCompletionArgs_DeleteTask );
}

void TaskSetFunc( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    (void)start_; (void)end_;
    struct TaskSetArgs* pTaskSetArgs        = pArgs_;
    struct enkiParamsTaskSet paramsTaskNext = enkiGetParamsTaskSet( pTaskSetArgs->pTask );
    if( 0 == start_ )
    {
         // for clarity in this example we only output one printf per taskset func called, but would normally loop from start_ to end_ doing work
        printf("Task %s for run %u running on thread %u has set size %u\n", pTaskSetArgs->name, pTaskSetArgs->run, threadnum_, paramsTaskNext.setSize);
    }

    // A TastSetFunction is not a safe place to free it's own pArgs_ as when the setSize > 1 there may be multiple
    // calls to this function with the same pArgs_
}


int main(int argc, const char * argv[])
{
    // This examples shows CompletionActions used to modify a following tasks parameters and free allocations
    // Task Graph for this example (with names shortened to fit on screen):
    // 
    // pTaskSetA
    //          ->pCompletionActionA-PreFunc-PostFunc
    //                                      ->pTaskSetB
    //                                                ->pCompletionActionB-(no PreFunc)-PostFunc
    //
    // Note that pTaskSetB must depend on pCompletionActionA NOT pTaskSetA or it could run at the same time as pCompletionActionA
    // so cannot be modified.

    struct enkiTaskSet*               pTaskSetA;
    struct enkiCompletionAction*      pCompletionActionA;
    struct enkiTaskSet*               pTaskSetB;
    struct enkiCompletionAction*      pCompletionActionB;
    struct TaskSetArgs*               pTaskSetArgsA;
    struct CompletionArgs_ModifyTask*           pCompletionArgsA;
    struct enkiParamsCompletionAction paramsCompletionActionA;
    struct TaskSetArgs*               pTaskSetArgsB;
    struct enkiDependency*            pDependencyOfTaskSetBOnCompletionActionA;
    struct CompletionArgs_DeleteTask* pCompletionArgs_DeleteTaskA;
    struct CompletionArgs_DeleteTask* pCompletionArgs_DeleteTaskB;
    struct enkiParamsCompletionAction paramsCompletionActionB;
    int run;

    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    for( run=0; run<10; ++run )
    {
        // Create all this runs tasks and completion actions
        pTaskSetA          = enkiCreateTaskSet( pETS, TaskSetFunc );
        pCompletionActionA = enkiCreateCompletionAction( pETS,
                                                    CompletionFunctionPreComplete_ModifyDependentTask,
                                                    CompletionFunctionPostComplete_DeleteTask );
        pTaskSetB          = enkiCreateTaskSet( pETS, TaskSetFunc );
        pCompletionActionB = enkiCreateCompletionAction( pETS,
                                                    NULL,
                                                    CompletionFunctionPostComplete_DeleteTask );

        // Set args for TaskSetA
        pTaskSetArgsA    = malloc(sizeof(struct TaskSetArgs));
        pTaskSetArgsA->run   = run;
        pTaskSetArgsA->pTask = pTaskSetA;
        pTaskSetArgsA->name  = "A";
        enkiSetArgsTaskSet( pTaskSetA, pTaskSetArgsA );

        // Set args for CompletionActionA, and make dependent on TaskSetA through pDependency
        pCompletionArgsA = malloc(sizeof(struct CompletionArgs_ModifyTask));
        pCompletionArgsA->pTaskB = pTaskSetB;
        pCompletionArgsA->run    = run;
        pCompletionArgs_DeleteTaskA = malloc(sizeof(struct CompletionArgs_DeleteTask));
        pCompletionArgs_DeleteTaskA->pTask             = pTaskSetA;
        pCompletionArgs_DeleteTaskA->pCompletionAction = pCompletionActionA;
        pCompletionArgs_DeleteTaskA->pDependency       = NULL;
        pCompletionArgs_DeleteTaskA->run               = run;

        paramsCompletionActionA = enkiGetParamsCompletionAction( pCompletionActionA );
        paramsCompletionActionA.pArgsPreComplete  = pCompletionArgsA;
        paramsCompletionActionA.pArgsPostComplete = pCompletionArgs_DeleteTaskA;
        paramsCompletionActionA.pDependency = enkiGetCompletableFromTaskSet( pTaskSetA );
        enkiSetParamsCompletionAction( pCompletionActionA, paramsCompletionActionA );


        // Set args for TaskSetB
        pTaskSetArgsB    = malloc(sizeof(struct TaskSetArgs));
        pTaskSetArgsB->run   = run;
        pTaskSetArgsB->pTask = pTaskSetB;
        pTaskSetArgsB->name  = "B";
        enkiSetArgsTaskSet( pTaskSetB, pTaskSetArgsB );

        // TaskSetB depends on pCompletionActionA
        pDependencyOfTaskSetBOnCompletionActionA = enkiCreateDependency( pETS );
        enkiSetDependency( pDependencyOfTaskSetBOnCompletionActionA,
                           enkiGetCompletableFromCompletionAction( pCompletionActionA ),
                           enkiGetCompletableFromTaskSet( pTaskSetB ) );

        // Set args for CompletionActionB, and make dependent on TaskSetB through pDependency
        pCompletionArgs_DeleteTaskB = malloc(sizeof(struct CompletionArgs_DeleteTask));
        pCompletionArgs_DeleteTaskB->pTask              = pTaskSetB;
        pCompletionArgs_DeleteTaskB->pDependency        = pDependencyOfTaskSetBOnCompletionActionA;
        pCompletionArgs_DeleteTaskB->pCompletionAction  = pCompletionActionB;
        pCompletionArgs_DeleteTaskB->run                = run;

        paramsCompletionActionB = enkiGetParamsCompletionAction( pCompletionActionB );
        paramsCompletionActionB.pArgsPreComplete  = NULL; // pCompletionActionB does not have a PreComplete function
        paramsCompletionActionB.pArgsPostComplete = pCompletionArgs_DeleteTaskB;
        paramsCompletionActionB.pDependency = enkiGetCompletableFromTaskSet( pTaskSetB );
        enkiSetParamsCompletionAction( pCompletionActionB, paramsCompletionActionB );


        // To launch all, we only add the first TaskSet
        enkiAddTaskSet( pETS, pTaskSetA );
    }
    enkiWaitForAll( pETS );

    enkiDeleteTaskScheduler( pETS );
}
