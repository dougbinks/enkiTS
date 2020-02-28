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


void TaskSetAFunc( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    (void)start_; (void)end_; (void)pArgs_;
    printf("A on thread %u\n", threadnum_);
}

void TaskSetBFunc( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    (void)start_; (void)end_; (void)pArgs_;
    printf("B on thread %u\n", threadnum_);
}

void PinnedTaskCFunc( void* pArgs_ )
{
    (void)pArgs_;
    printf("C Pinned task on thread 0, should be %u\n", enkiGetThreadNum( pETS ) );
}

void TaskSetDFunc( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    (void)start_; (void)end_; (void)pArgs_;
    printf("D on thread %u\n", threadnum_);
}

#define NUM_TASK_B 4
#define NUM_TASK_D 2

int main(int argc, const char * argv[])
{
    int run;
    enkiTaskSet*        pTaskA;
    enkiTaskSet*        pTaskB[NUM_TASK_B];
    enkiDependency*     pTaskBDependencyToA[NUM_TASK_B];
    enkiPinnedTask*     pPinnedTaskC;
    enkiDependency*     pPinnedTaskCDependencyToBs[NUM_TASK_B];
    enkiTaskSet*        pTaskD[NUM_TASK_D];
    enkiDependency*     pTaskDDependencyToC[NUM_TASK_D];
    enkiCompletable*    pCompletableFinished; // A completable can be used on it's own to check if tasks complete.
    enkiDependency*     pDependencyToD[NUM_TASK_D];

    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    // create tasks and set dependencies once, reuse many times
    pTaskA            = enkiCreateTaskSet( pETS, TaskSetAFunc );
    for( int i=0; i<NUM_TASK_B; ++i )
    {
        pTaskB[i]              = enkiCreateTaskSet( pETS, TaskSetBFunc );
        pTaskBDependencyToA[i] = enkiCreateDependency( pETS );
        enkiSetDependency(
            pTaskBDependencyToA[i],
            enkiGetCompletableFromTaskSet( pTaskA ),
            enkiGetCompletableFromTaskSet( pTaskB[i] )
            );
    }
    pPinnedTaskC = enkiCreatePinnedTask( pETS, PinnedTaskCFunc, 0 );
    for( int i=0; i<NUM_TASK_B; ++i )
    {
        pPinnedTaskCDependencyToBs[i] = enkiCreateDependency( pETS );
        enkiSetDependency(
            pPinnedTaskCDependencyToBs[i],
            enkiGetCompletableFromTaskSet( pTaskB[i] ),
            enkiGetCompletableFromPinnedTask( pPinnedTaskC )
            );
    }
    for( int i=0; i<NUM_TASK_D; ++i )
    {
        pTaskD[i]              = enkiCreateTaskSet( pETS, TaskSetDFunc );
        pTaskDDependencyToC[i] = enkiCreateDependency( pETS );
        enkiSetDependency(
            pTaskDDependencyToC[i],
            enkiGetCompletableFromPinnedTask( pPinnedTaskC ),
            enkiGetCompletableFromTaskSet( pTaskD[i] )
            );
    }
    pCompletableFinished = enkiCreateCompletable( pETS );
    for( int i=0; i<NUM_TASK_D; ++i )
    {
        pDependencyToD[i] = enkiCreateDependency( pETS );
        enkiSetDependency(
            pDependencyToD[i],
            enkiGetCompletableFromTaskSet( pTaskD[i] ),
            pCompletableFinished
            );
    }


    // run task graph as many times as you like by adding first task,
    // and waiting for last (if needed).
    for( run=0; run<10; ++run )
    {
        printf("Starting run %d\n", run);
        enkiAddTaskSetToPipe( pETS, pTaskA, NULL, 1);
        enkiWaitForCompletable( pETS, pCompletableFinished );
        printf("FINISHED run %d\n", run);
    }



    // new delete functions require task scheduler argument
    // as this reduces memory requirements
    for( int i=0; i<NUM_TASK_D; ++i )
    {
        enkiDeleteDependency( pETS, pDependencyToD[i] );
    }
    enkiDeleteCompletable( pETS, pCompletableFinished );
    for( int i=0; i<NUM_TASK_D; ++i )
    {
        enkiDeleteDependency( pETS, pTaskDDependencyToC[i] );
        enkiDeleteTaskSet( pTaskD[i] );
    }
    for( int i=0; i<NUM_TASK_B; ++i )
    {
        enkiDeleteDependency( pETS, pPinnedTaskCDependencyToBs[i] );
    }
    enkiDeletePinnedTask( pPinnedTaskC );
    for( int i=0; i<NUM_TASK_B; ++i )
    {
        enkiDeleteDependency( pETS, pTaskBDependencyToA[i] );
        enkiDeleteTaskSet( pTaskB[i] );
    }
    enkiDeleteTaskSet( pTaskA );


    enkiDeleteTaskScheduler( pETS );
}