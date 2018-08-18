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
enkiTaskSet*        pPSumTask;
enkiTaskSet*        pPSumReductionTask;


typedef struct ParallelSumTaskSetArgs
{
    uint64_t* pPartialSums;
    uint32_t  numPartialSums;
} ParallelSumTaskSetArgs;

void ParallelSumTaskSetArgsInit( ParallelSumTaskSetArgs* pArgs_ )
{
    pArgs_->numPartialSums = enkiGetNumTaskThreads( pETS );
    pArgs_->pPartialSums = (uint64_t*)malloc( sizeof(uint64_t) * pArgs_->numPartialSums );
    memset( pArgs_->pPartialSums, 0, sizeof(uint64_t) * pArgs_->numPartialSums );
}

void ParallelSumTaskSetFunc( uint32_t start_, uint32_t end, uint32_t threadnum_, void* pArgs_ )
{
    ParallelSumTaskSetArgs args;
    uint64_t sum, i;
    
    args = *(ParallelSumTaskSetArgs*)pArgs_;

    sum = args.pPartialSums[threadnum_];
    for( i = start_; i < end; ++i )
    {
        sum += i + 1;
    }
    args.pPartialSums[threadnum_] = sum;
}

void ParallelReductionSumTaskSet(  uint32_t start_, uint32_t end, uint32_t threadnum_, void* pArgs_ )
{
    ParallelSumTaskSetArgs args;
    uint64_t sum;
    uint64_t inMax_outSum, i;

    inMax_outSum = *(uint64_t*)pArgs_;

    ParallelSumTaskSetArgsInit( &args );

    enkiAddTaskSetToPipe( pETS, pPSumTask, &args, (uint32_t)inMax_outSum);
    enkiWaitForTaskSet( pETS, pPSumTask );

    sum = 0;
    for( i = 0; i < args.numPartialSums; ++i )
    {
        sum += args.pPartialSums[i];
    }

    free( args.pPartialSums );


    *(uint64_t*)pArgs_ = sum;
}



int main(int argc, const char * argv[])
{
    uint64_t inMax_outSum, i, serialSum, max;


    pETS = enkiNewTaskScheduler();
    enkiInitTaskScheduler( pETS );

    pPSumTask            = enkiCreateTaskSet( pETS, ParallelSumTaskSetFunc );
    pPSumReductionTask    = enkiCreateTaskSet( pETS, ParallelReductionSumTaskSet );

    max = 10 * 1024 * 1024;
    inMax_outSum = max;
    enkiAddTaskSetToPipe( pETS, pPSumReductionTask, &inMax_outSum, 1);
    enkiWaitForTaskSet( pETS, pPSumReductionTask );

    printf("Parallel Example complete sum: \t %llu\n", (long long unsigned int)inMax_outSum );

    serialSum = 0;
    for( i = 0; i < max; ++i )
    {
        serialSum += i + 1;
    }

    printf("Serial Example complete sum: \t %llu\n", (long long unsigned int)serialSum );

    enkiDeleteTaskSet( pPSumTask );
    enkiDeleteTaskSet( pPSumReductionTask );

    enkiDeleteTaskScheduler( pETS );
}