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

#include "TaskScheduler.h"
#include "Timer.h"

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <vector>
#include <algorithm>

#ifndef _WIN32
    #include <string.h>
#endif

using namespace enki;





TaskScheduler g_TS;
uint32_t      g_numTestsRun       = 0;
uint32_t      g_numTestsSucceeded = 0;

void RunTestFunction(  const char* pTestFuncName_, std::function<bool ()> TestFunc )
{
    ++g_numTestsRun;
    fprintf(stdout, "\nRunning: Test %2u: %s...\n", g_numTestsRun, pTestFuncName_ );
    bool bSuccess = TestFunc();
    if( bSuccess )
    {
        fprintf(stdout, "SUCCESS: Test %2u: %s.\n", g_numTestsRun, pTestFuncName_ );
        ++g_numTestsSucceeded;
    }
    else
    {
        fprintf(stderr, "FAILURE: Test %2u: %s.\n", g_numTestsRun, pTestFuncName_ );
    }
}

struct ParallelSumTaskSet : ITaskSet
{
    struct Count
    {
        // prevent false sharing.
        uint64_t    count;
        char        cacheline[64];
    };
    Count*    m_pPartialSums;
    uint32_t  m_NumPartialSums;

    ParallelSumTaskSet( uint32_t size_ ) : m_pPartialSums(NULL), m_NumPartialSums(0) { m_SetSize = size_; }
    virtual ~ParallelSumTaskSet()
    {
        delete[] m_pPartialSums;
    }

    void Init( uint32_t numPartialSums_ )
    {
        delete[] m_pPartialSums;
        m_NumPartialSums =numPartialSums_ ;
        m_pPartialSums = new Count[ m_NumPartialSums ];
        memset( m_pPartialSums, 0, sizeof(Count)*m_NumPartialSums );
    }

    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
    {
        assert( m_pPartialSums && m_NumPartialSums );
        uint64_t sum = m_pPartialSums[threadnum_].count;
        for( uint64_t i = range_.start; i < range_.end; ++i )
        {
            sum += i + 1;
        }
        m_pPartialSums[threadnum_].count = sum;
    }
  
};

struct ParallelReductionSumTaskSet : ITaskSet
{
    ParallelSumTaskSet* m_pParallelSum;
    Dependency          m_Dependency;
    uint64_t            m_FinalSum;

    ParallelReductionSumTaskSet( ParallelSumTaskSet* pParallelSum_ ) : m_pParallelSum( pParallelSum_ ), m_Dependency( pParallelSum_, this ), m_FinalSum(0)
    {
    }

    void ExecuteRange( TaskSetPartition range, uint32_t threadnum ) override
    {
        for( uint32_t i = 0; i < m_pParallelSum->m_NumPartialSums; ++i )
        {
            m_FinalSum += m_pParallelSum->m_pPartialSums[i].count;
        }
    }
};

void threadFunction( uint32_t setSize_, bool* pbRegistered_, uint64_t* pSumParallel_ )
{
    *pbRegistered_ = g_TS.RegisterExternalTaskThread();
    if( *pbRegistered_ )
    {
        ParallelSumTaskSet          parallelSumTask( setSize_ );
        parallelSumTask.Init( g_TS.GetNumTaskThreads() );
        ParallelReductionSumTaskSet parallelReductionSumTaskSet( &parallelSumTask );

        g_TS.AddTaskSetToPipe( &parallelSumTask );
        g_TS.WaitforTask( &parallelReductionSumTaskSet );

        g_TS.DeRegisterExternalTaskThread();
        *pSumParallel_ = parallelReductionSumTaskSet.m_FinalSum;
    }
}

struct PinnedTask : IPinnedTask
{
    PinnedTask()
        : IPinnedTask( enki::GetNumHardwareThreads() - 1 ) // set pinned thread to 0
    {}
    virtual void Execute()
    {
        threadRunOn = g_TS.GetThreadNum();
    }
    uint32_t threadRunOn = 0;
};


struct TestPriorities : ITaskSet
{
    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
    {
    }
};

struct CustomAllocData
{
    const char* domainName;
    uint64_t totalAllocations;
};

void* CustomAllocFunc( size_t align_, size_t size_, void* userData_, const char* file_, int line_ )
{
    CustomAllocData* data = (CustomAllocData*)userData_;
    data->totalAllocations += size_;
    return DefaultAllocFunc( align_, size_, userData_, file_, line_ );
};

void  CustomFreeFunc(  void* ptr_,    size_t size_, void* userData_, const char* file_, int line_ )
{
    CustomAllocData* data = (CustomAllocData*)userData_;
    data->totalAllocations -= size_;
    DefaultFreeFunc( ptr_, size_, userData_, file_, line_ );
};

std::atomic<int32_t> gs_DependencyCounter = {0};

struct TestDependenciesTaskSet : ITaskSet
{
    int32_t                 m_Counter = 0;
    std::vector<Dependency> m_Dependencies;
    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
    {
        m_Counter = gs_DependencyCounter.fetch_add(1);
    }
};

struct TestDependenciesPinnedTask : IPinnedTask
{
    int32_t                 m_Counter = 0;
    std::vector<Dependency> m_Dependencies;
    void Execute() override
    {
        m_Counter = gs_DependencyCounter.fetch_add(1);
    }
};

struct TestDependenciesCompletable : ICompletable
{
    std::vector<Dependency> m_Dependencies;
};

int main(int argc, const char * argv[])
{
    fprintf( stdout,"\n---Running Tests----\n" );

    enki::TaskSchedulerConfig baseConfig;
    fprintf( stdout,"System has %u hardware threads reported\n", baseConfig.numTaskThreadsToCreate + 1 );
    if( 0 == baseConfig.numTaskThreadsToCreate )
    {
        baseConfig.numTaskThreadsToCreate = 1;
        fprintf( stdout,"As only one hardware thread forcing enkiTS to use 2 threads\n");
    }

    uint32_t setSize = 20 * 1024 * 1024;
    uint64_t sumSerial;

    // evaluate serial for test comparison with parallel runs
    ParallelSumTaskSet serialTask( setSize );
    serialTask.Init( 1 );
    TaskSetPartition range = { 0, setSize };
    serialTask.ExecuteRange( range, 0 );
    sumSerial = serialTask.m_pPartialSums[0].count;



    RunTestFunction(
        "Test Lots of TaskSets",
        [&]()->bool
        {
            g_TS.Initialize( baseConfig );

            static constexpr uint32_t TASK_RANGE = 65*65;
            static constexpr uint32_t TASK_COUNT = 50;


            struct TaskSet : public enki::ITaskSet
            {
                TaskSet() : enki::ITaskSet(TASK_RANGE) {};
                virtual void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_  ) override
                {
                    if( range_.start >= TASK_RANGE && range_.end > TASK_RANGE )
                    {
                        countErrors.fetch_add(1);
                    }
                }

                std::atomic<int32_t> countErrors{ 0 };
            };

            TaskSet tasks[TASK_COUNT];

            for( uint32_t i = 0; i < TASK_COUNT; ++i )
            {
                g_TS.AddTaskSetToPipe( &tasks[i] );
            }

            g_TS.WaitforAll();

            bool bSuccess = true;
            for( uint32_t i = 0; i < TASK_COUNT; ++i )
            {
                if( tasks[i].countErrors.load( std::memory_order_relaxed ) > 0 )
                {
                    bSuccess = false;
                    break;
                }
            }

            return bSuccess;
        }
    );
    RunTestFunction(
        "Parallel Reduction Sum",
        [&]()->bool
        {
            g_TS.Initialize( baseConfig );
            ParallelSumTaskSet          parallelSumTask( setSize );
            parallelSumTask.Init( g_TS.GetNumTaskThreads() );
            ParallelReductionSumTaskSet parallelReductionSumTaskSet( &parallelSumTask );

            g_TS.AddTaskSetToPipe( &parallelSumTask );
            g_TS.WaitforTask( &parallelReductionSumTaskSet );

            fprintf( stdout,"\tParallelReductionSum: %" PRIu64 ", sumSerial: %" PRIu64 "\n", parallelReductionSumTaskSet.m_FinalSum, sumSerial );
            return parallelReductionSumTaskSet.m_FinalSum == sumSerial;
        } );

    RunTestFunction(
        "External Thread",
        [&]()->bool
        {
            enki::TaskSchedulerConfig config = baseConfig;
            config.numExternalTaskThreads = 1;
            bool bRegistered = false;
            uint64_t sumParallel = 0;
            g_TS.Initialize( config );

            std::thread threads( threadFunction, setSize, &bRegistered, &sumParallel );
            threads.join();
            fprintf( stdout,"\tExternal thread sum: %" PRIu64 ", sumSerial: %" PRIu64 "\n", sumParallel, sumSerial );
            if( !bRegistered )
            {
                fprintf( stderr,"\tExternal thread did not register\n" );
                return false;
            }
            if( sumParallel != sumSerial )
            {
                return false;
            }
            return true;
        } );

    RunTestFunction(
        "Pinned Task",
        [&]()->bool
        {
            g_TS.Initialize( baseConfig );
            PinnedTask pinnedTask;
            g_TS.AddPinnedTask( &pinnedTask );
            g_TS.WaitforTask( &pinnedTask );
            fprintf( stdout,"\tPinned task ran on thread %u, requested thread %u\n", pinnedTask.threadRunOn, pinnedTask.threadNum );
            return pinnedTask.threadRunOn == pinnedTask.threadNum;
        } );

    RunTestFunction(
        "Priorities",
        [&]()->bool
        {
            // check priorities run in order by forcing single threaded execution
            enki::TaskSchedulerConfig config = baseConfig;
            config.numTaskThreadsToCreate = 0;
            g_TS.Initialize( config );
            TestPriorities priorityTaskLow;
            priorityTaskLow.m_Priority = enki::TASK_PRIORITY_LOW;
            TestPriorities priorityTaskHigh;
            priorityTaskHigh.m_Priority = enki::TASK_PRIORITY_HIGH;
            g_TS.AddTaskSetToPipe( &priorityTaskLow );
            g_TS.AddTaskSetToPipe( &priorityTaskHigh );
            g_TS.WaitforTask( &priorityTaskHigh, priorityTaskHigh.m_Priority );

            // WaitforTask should not have been run any task below high priority,
            // even though low priority task was added first
            if( priorityTaskLow.GetIsComplete() )
            {
                return false;
            }

            g_TS.WaitforTask( &priorityTaskLow );

            return true;
        } );

    RunTestFunction(
        "Custom Allocator",
        [&]()->bool
        {
            enki::TaskSchedulerConfig config = baseConfig;
            config.customAllocator.alloc = CustomAllocFunc;
            config.customAllocator.free  = CustomFreeFunc;
            CustomAllocData customAllocdata{ "enkITS", 0 };
            config.customAllocator.userData = &customAllocdata;

            g_TS.Initialize( config );
            uint64_t allocsAfterInit = customAllocdata.totalAllocations;
            fprintf( stdout,"\tenkiTS allocated bytes after init: %" PRIu64 "\n", customAllocdata.totalAllocations );

            ParallelSumTaskSet          parallelSumTask( setSize );
            parallelSumTask.Init( g_TS.GetNumTaskThreads() );
            ParallelReductionSumTaskSet parallelReductionSumTaskSet( &parallelSumTask );
            g_TS.AddTaskSetToPipe( &parallelSumTask );
            g_TS.WaitforTask( &parallelReductionSumTaskSet );

            fprintf( stdout,"\tenkiTS allocated bytes after running tasks: %" PRIu64 "\n", customAllocdata.totalAllocations );
            if( customAllocdata.totalAllocations != allocsAfterInit )
            {
                fprintf( stderr,"\tERROR: enkiTS allocated bytes during scheduling\n" );
                return false;
            }
            g_TS.WaitforAllAndShutdown();
            fprintf( stdout,"\tenkiTS allocated bytes after shutdown: %" PRIu64 "\n", customAllocdata.totalAllocations );
            return customAllocdata.totalAllocations == 0;
        } );

    RunTestFunction(
        "Dependencies",
        [&]()->bool
        {
            g_TS.Initialize( baseConfig );

            TestDependenciesTaskSet taskSetA;

            TestDependenciesTaskSet taskSetBs[8];
            for( auto& task : taskSetBs )
            {
                task.SetDependenciesVec(task.m_Dependencies,{&taskSetA});
            }

            TestDependenciesPinnedTask pinnedTaskC;
            pinnedTaskC.SetDependenciesVec(pinnedTaskC.m_Dependencies, taskSetBs);

            TestDependenciesTaskSet taskSetDs[8];
            for( auto& task : taskSetDs )
            {
                task.SetDependenciesVec(task.m_Dependencies,{&pinnedTaskC});
            }
            TestDependenciesTaskSet taskSetEs[4];
            for( auto& task : taskSetEs )
            {
                task.SetDependenciesVec(task.m_Dependencies,taskSetDs);
            }

            TestDependenciesCompletable finalTask;
            finalTask.SetDependenciesVec( finalTask.m_Dependencies,taskSetEs);

            g_TS.AddTaskSetToPipe( &taskSetA );
            g_TS.WaitforTask( &finalTask );

            // check counters
            int32_t lastCount = taskSetA.m_Counter;
            int32_t countCheck = lastCount;
            for( auto& task : taskSetBs )
            {
                if( task.m_Counter < countCheck )
                {
                    fprintf( stderr,"\tERROR: enkiTS dependencies issue %d < %d at line %d\n", task.m_Counter, lastCount, __LINE__ );
                    return false;
                }
                lastCount = std::max( lastCount, task.m_Counter );
            }
            countCheck = lastCount;
            if( pinnedTaskC.m_Counter < countCheck )
            {
                fprintf( stderr,"\tERROR: enkiTS dependencies issue %d < %d at line %d\n", pinnedTaskC.m_Counter, lastCount, __LINE__ );
                return false;
                lastCount = std::max( lastCount, pinnedTaskC.m_Counter );
            }
            countCheck = lastCount;
            for( auto& task : taskSetDs )
            {
                if( task.m_Counter < countCheck )
                {
                    fprintf( stderr,"\tERROR: enkiTS dependencies issue %d < %d at line %d\n", task.m_Counter, lastCount, __LINE__ );
                    return false;
                }
                lastCount = std::max( lastCount, task.m_Counter );
            }
            countCheck = lastCount;
            for( auto& task : taskSetEs )
            {
                if( task.m_Counter < countCheck )
                {
                    fprintf( stderr,"\tERROR: enkiTS dependencies issue %d < %d at line %d\n", task.m_Counter, lastCount, __LINE__ );
                    return false;
                }
                lastCount = std::max( lastCount, task.m_Counter );
            }
            g_TS.WaitforAllAndShutdown();
            return true;
        } );

    RunTestFunction(
        "WaitForNewPinnedTasks",
        [&]()->bool
        {
            enki::TaskSchedulerConfig config = baseConfig;
            config.numTaskThreadsToCreate += 1;
            g_TS.Initialize( config );
            const uint32_t PINNED_ONLY_THREAD = g_TS.GetNumTaskThreads() - 1;

            LambdaPinnedTask waitTask( PINNED_ONLY_THREAD, []()
            { 
                while( g_TS.GetIsWaitforAllCalled() )
                {
                    g_TS.WaitForNewPinnedTasks();
                    g_TS.RunPinnedTasks();
                }
            } );

            g_TS.AddPinnedTask( &waitTask );

            PinnedTask pinnedTask;
            pinnedTask.threadNum = PINNED_ONLY_THREAD;
            g_TS.AddPinnedTask( &pinnedTask );
            g_TS.WaitforTask( &pinnedTask );
            fprintf( stdout,"\tPinned task ran on thread %u, requested thread %u\n", pinnedTask.threadRunOn, pinnedTask.threadNum );
            if( pinnedTask.threadRunOn != pinnedTask.threadNum )
            {
                return false;
            }

            g_TS.WaitforAll(); // force all tasks to end, waitTask should exit because we use GetIsWaitforAllCalled()

            g_TS.AddPinnedTask( &waitTask );
            g_TS.AddPinnedTask( &pinnedTask );
            g_TS.WaitforTask( &pinnedTask );
            fprintf( stdout,"\tPinned task ran on thread %u, requested thread %u\n", pinnedTask.threadRunOn, pinnedTask.threadNum );

            g_TS.WaitforAllAndShutdown();

            return pinnedTask.threadRunOn == pinnedTask.threadNum;
        } );

    fprintf( stdout, "\n%u Tests Run\n%u Tests Succeeded\n\n", g_numTestsRun, g_numTestsSucceeded );
    if( g_numTestsRun == g_numTestsSucceeded )
    {
        fprintf( stdout, "All tests SUCCEEDED\n" );
    }
    else
    {
        fprintf( stderr, "%u tests FAILED\n", g_numTestsRun - g_numTestsSucceeded );
        return 1;
    }
    return 0;
}
