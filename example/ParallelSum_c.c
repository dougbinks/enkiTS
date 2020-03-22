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


typedef struct ParallelSumTaskSetArgs
{
    uint64_t* pPartialSums;
    uint32_t  numPartialSums;
} ParallelSumTaskSetArgs;

void ParallelSumTaskSetFunc( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    ParallelSumTaskSetArgs args;
    uint64_t sum, i;
    
    args = *(ParallelSumTaskSetArgs*)pArgs_;

    sum = args.pPartialSums[threadnum_];
    for( i = start_; i < end_; ++i )
    {
        sum += i + 1;
    }
    args.pPartialSums[threadnum_] = sum;
}

typedef struct ParallelReductionSumTaskSetArgs
{
    ParallelSumTaskSetArgs sumArgs;
    uint32_t               numParrallelSums;
    enkiTaskSet*           pPSumTask;
    uint64_t               sum;
} ParallelReductionSumTaskSetArgs;

void ParallelReductionSumTaskSetFunc(  uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ )
{
    ParallelReductionSumTaskSetArgs* pArgs;
    uint64_t sum, i;

    pArgs = (ParallelReductionSumTaskSetArgs*)pArgs_;

    sum = 0;
    for( i = 0; i < pArgs->sumArgs.numPartialSums; ++i )
    {
        sum += pArgs->sumArgs.pPartialSums[i];
    }

    pArgs->sum = sum;
}

static const uint32_t REPEATS = 10;

int main(int argc, const char * argv[])
{
    uint32_t run;
    uint64_t i, serialSum;
    ParallelReductionSumTaskSetArgs parRedSumTaskSetArgs;
    enkiTaskSet*                    pPSumTask;
    enkiTaskSet*                    pPSumReductionTask;
    enkiDependency*                 pDependencypPSumReductionOnpPSum;

    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    pPSumTask          = enkiCreateTaskSet( pETS, ParallelSumTaskSetFunc );
    pPSumReductionTask = enkiCreateTaskSet( pETS, ParallelReductionSumTaskSetFunc );
    pDependencypPSumReductionOnpPSum = enkiCreateDependency( pETS );
    enkiSetDependency( pDependencypPSumReductionOnpPSum,
                       enkiGetCompletableFromTaskSet( pPSumTask ),
                       enkiGetCompletableFromTaskSet( pPSumReductionTask ) );

    parRedSumTaskSetArgs.pPSumTask = pPSumTask;
    parRedSumTaskSetArgs.numParrallelSums = 10 * 1024 * 1024;
    parRedSumTaskSetArgs.sumArgs.numPartialSums = enkiGetNumTaskThreads( pETS );
    parRedSumTaskSetArgs.sumArgs.pPartialSums = (uint64_t*)malloc(
            sizeof(uint64_t) * parRedSumTaskSetArgs.sumArgs.numPartialSums );
    enkiSetArgsTaskSet( pPSumReductionTask, &parRedSumTaskSetArgs );

    struct enkiParamsTaskSet parSumTaskParams = enkiGetParamsTaskSet( pPSumTask );
    parSumTaskParams.pArgs   = &parRedSumTaskSetArgs.sumArgs;
    parSumTaskParams.setSize = parRedSumTaskSetArgs.numParrallelSums;
    enkiSetParamsTaskSet( pPSumTask, parSumTaskParams );

    for( run = 0; run< REPEATS; ++run )
    {
        // reset partial sums
        memset( parRedSumTaskSetArgs.sumArgs.pPartialSums, 0,
            sizeof(uint64_t) * parRedSumTaskSetArgs.sumArgs.numPartialSums );

        // add first task, parallel sum
        enkiAddTaskSet( pETS, pPSumTask );

        // wait for reduction which will run due to dependencies
        enkiWaitForTaskSet( pETS, pPSumReductionTask );

        printf("Parallel Example complete sum: \t %llu\n", (long long unsigned int)parRedSumTaskSetArgs.sum );

        serialSum = 0;
        for( i = 0; i < parRedSumTaskSetArgs.numParrallelSums; ++i )
        {
            serialSum += i + 1;
        }

        printf("Serial Example complete sum: \t %llu\n", (long long unsigned int)serialSum );

        if( serialSum != parRedSumTaskSetArgs.sum )
        {
            printf("ERROR: Serial sum does not match parallel sum\n");
        }
    }

    free( parRedSumTaskSetArgs.sumArgs.pPartialSums );

    enkiDeleteDependency( pETS, pDependencypPSumReductionOnpPSum );
    enkiDeleteTaskSet( pETS, pPSumReductionTask );
    enkiDeleteTaskSet( pETS, pPSumTask );

    enkiDeleteTaskScheduler( pETS );
}
