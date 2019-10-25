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

#include <assert.h>

#include "TaskScheduler.h"
#include "LockLessMultiReadPipe.h"

#include <algorithm>

#if defined __i386__ || defined __x86_64__
#include "x86intrin.h"
#elif defined _WIN32
#include <intrin.h>
#endif

using namespace enki;


static const uint32_t PIPESIZE_LOG2              = 8;
static const uint32_t SPIN_COUNT                 = 10;
static const uint32_t SPIN_BACKOFF_MULTIPLIER    = 100;
static const uint32_t MAX_NUM_INITIAL_PARTITIONS = 8;
static const uint32_t CACHE_LINE_SIZE            = 64; // awaiting std::hardware_constructive_interference_size

// thread_local not well supported yet by C++11 compilers.
#ifdef _MSC_VER
    #if _MSC_VER <= 1800
        #define thread_local __declspec(thread)
    #endif
#elif __APPLE__
        // Apple thread_local currently not implemented despite it being in Clang.
        #define thread_local __thread
#endif


// each software thread gets it's own copy of gtl_threadNum, so this is safe to use as a static variable
static thread_local uint32_t                             gtl_threadNum       = 0;

namespace enki 
{
    struct SubTaskSet
    {
        ITaskSet*           pTask;
        TaskSetPartition    partition;
    };

    // we derive class TaskPipe rather than typedef to get forward declaration working easily
    class TaskPipe : public LockLessMultiReadPipe<PIPESIZE_LOG2,enki::SubTaskSet> {};

    enum ThreadState : int32_t
    {
        THREAD_STATE_RUNNING,
        THREAD_STATE_WAIT_TASK_COMPLETION,
        THREAD_STATE_EXTERNAL_REGISTERED,
        THREAD_STATE_EXTERNAL_UNREGISTERED,
        THREAD_STATE_WAIT_NEW_TASKS,
        THREAD_STATE_STOPPED,
    };

    struct ThreadArgs
    {
        uint32_t                 threadNum;
        TaskScheduler*           pTaskScheduler;
    };

    struct ThreadDataStore
    {
        std::atomic<ThreadState> threadState;
        char prevent_false_Share[ CACHE_LINE_SIZE - sizeof(std::atomic<ThreadState>) ];
    };
    static_assert( sizeof( ThreadDataStore ) >= CACHE_LINE_SIZE, "ThreadDataStore may exhibit false sharing" );

    class PinnedTaskList : public LocklessMultiWriteIntrusiveList<IPinnedTask> {};

    semaphoreid_t* SemaphoreCreate();
    void SemaphoreDelete( semaphoreid_t* pSemaphore_ );
    void SemaphoreWait(   semaphoreid_t& semaphoreid );
    void SemaphoreSignal( semaphoreid_t& semaphoreid, int32_t countWaiting );
}

namespace
{
    SubTaskSet SplitTask( SubTaskSet& subTask_, uint32_t rangeToSplit_ )
    {
        SubTaskSet splitTask = subTask_;
        uint32_t rangeLeft = subTask_.partition.end - subTask_.partition.start;

        if( rangeToSplit_ > rangeLeft )
        {
            rangeToSplit_ = rangeLeft;
        }
        splitTask.partition.end = subTask_.partition.start + rangeToSplit_;
        subTask_.partition.start = splitTask.partition.end;
        return splitTask;
    }

    #if ( defined _WIN32 && ( defined _M_IX86  || defined _M_X64 ) ) || ( defined __i386__ || defined __x86_64__ )
    // Note: see https://software.intel.com/en-us/articles/a-common-construct-to-avoid-the-contention-of-threads-architecture-agnostic-spin-wait-loops
    static void SpinWait( uint32_t spinCount_ )
    {
        uint64_t end = __rdtsc() + spinCount_;
        while( __rdtsc() < end )
        {
            _mm_pause();
        }        
    }
    #else
    static void SpinWait( uint32_t spinCount_ )
    {
        while( spinCount_ )
        {
            // TODO: may have NOP or yield equiv
            --spinCount_;
        }        
    }
    #endif
}

static void SafeCallback( ProfilerCallbackFunc func_, uint32_t threadnum_ )
{
    if( func_ != nullptr )
    {
        func_( threadnum_ );
    }
}

ENKITS_API bool enki::TaskScheduler::RegisterExternalTaskThread()
{
    bool bRegistered = false;
    while( !bRegistered && m_NumExternalTaskThreadsRegistered < (int32_t)m_Config.numExternalTaskThreads  )
    {
        for(uint32_t thread = 1; thread <= m_Config.numExternalTaskThreads; ++thread )
        {
            // ignore our thread
            ThreadState threadStateExpected = THREAD_STATE_EXTERNAL_UNREGISTERED;
            if( m_pThreadDataStore[thread].threadState.compare_exchange_strong(
                threadStateExpected, THREAD_STATE_EXTERNAL_REGISTERED ) )
            {
                ++m_NumExternalTaskThreadsRegistered;
                gtl_threadNum = thread;
                bRegistered = true;
                break;
            }
        }
    }
    return bRegistered;
}

ENKITS_API void enki::TaskScheduler::DeRegisterExternalTaskThread()
{
    assert( gtl_threadNum );
    --m_NumExternalTaskThreadsRegistered;
    m_pThreadDataStore[gtl_threadNum].threadState.store( THREAD_STATE_EXTERNAL_UNREGISTERED, std::memory_order_release );
    gtl_threadNum = 0;
}

ENKITS_API uint32_t enki::TaskScheduler::GetNumRegisteredExternalTaskThreads()
{
    return m_NumExternalTaskThreadsRegistered;
}


void TaskScheduler::TaskingThreadFunction( const ThreadArgs& args_ )
{
    uint32_t threadNum  = args_.threadNum;
    TaskScheduler*  pTS = args_.pTaskScheduler;
    gtl_threadNum       = threadNum;

    SafeCallback( pTS->m_Config.profilerCallbacks.threadStart, threadNum );

    uint32_t spinCount = 0;
    uint32_t hintPipeToCheck_io = threadNum + 1;    // does not need to be clamped.
    while( pTS->m_bRunning.load( std::memory_order_relaxed ) )
    {
        if( !pTS->TryRunTask( threadNum, hintPipeToCheck_io ) )
        {
            // no tasks, will spin then wait
            ++spinCount;
            if( spinCount > SPIN_COUNT )
            {
                pTS->WaitForNewTasks( threadNum );
                spinCount = 0;
            }
            else
            {
                uint32_t spinBackoffCount = spinCount * SPIN_BACKOFF_MULTIPLIER;
                SpinWait( spinBackoffCount );
            }
        }
        else
        {
            spinCount = 0; // have run a task so reset spin count.
        }
    }

    pTS->m_NumInternalTaskThreadsRunning.fetch_sub( 1, std::memory_order_release );
    SafeCallback( pTS->m_Config.profilerCallbacks.threadStop, threadNum );
    pTS->m_pThreadDataStore[threadNum].threadState.store( THREAD_STATE_STOPPED, std::memory_order_release );
    return;

}


void TaskScheduler::StartThreads()
{
    if( m_bHaveThreads )
    {
        return;
    }

    m_NumThreads = m_Config.numTaskThreadsToCreate + m_Config.numExternalTaskThreads + 1;

    for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
    {
        m_pPipesPerThread[ priority ]          = new TaskPipe[ m_NumThreads ];
        m_pPinnedTaskListPerThread[ priority ] = new PinnedTaskList[ m_NumThreads ];
    }

    m_pNewTaskSemaphore      = SemaphoreCreate();
    m_pTaskCompleteSemaphore = SemaphoreCreate();

    // we create one less thread than m_NumThreads as the main thread counts as one
    m_pThreadDataStore   = new ThreadDataStore[m_NumThreads];
    m_pThreads          = new std::thread*[m_NumThreads];
    m_bRunning = 1;

    for( uint32_t thread = 0; thread < m_Config.numExternalTaskThreads + 1; ++thread )
    {
        m_pThreadDataStore[thread].threadState    = THREAD_STATE_EXTERNAL_UNREGISTERED;
        m_pThreads[thread]                       = nullptr;
    }
    for( uint32_t thread = m_Config.numExternalTaskThreads + 1; thread < m_NumThreads; ++thread )
    {
        m_pThreadDataStore[thread].threadState    = THREAD_STATE_RUNNING;
        m_pThreads[thread]                       = new std::thread( TaskingThreadFunction, ThreadArgs{ thread, this } );
        ++m_NumInternalTaskThreadsRunning;
    }

    // Thread 0 is intialize thread and registered
    m_pThreadDataStore[0].threadState    = THREAD_STATE_EXTERNAL_REGISTERED;

    // ensure we have sufficient tasks to equally fill either all threads including main
    // or just the threads we've launched, this is outside the firstinit as we want to be able
    // to runtime change it
    if( 1 == m_NumThreads )
    {
        m_NumPartitions = 1;
        m_NumInitialPartitions = 1;
    }
    else
    {
        // There could be more threads than hardware threads if external threads are
        // being intended for blocking functionality such as io etc.
        // We only need to partition for a maximum of the available processor parallelism.
        uint32_t numThreadsToPartitionFor = m_NumThreads;
        numThreadsToPartitionFor = std::min( m_NumThreads, GetNumHardwareThreads() );
        m_NumPartitions = numThreadsToPartitionFor * (numThreadsToPartitionFor - 1);
        m_NumInitialPartitions = numThreadsToPartitionFor - 1;
        if( m_NumInitialPartitions > MAX_NUM_INITIAL_PARTITIONS )
        {
            m_NumInitialPartitions = MAX_NUM_INITIAL_PARTITIONS;
        }
    }

    m_bHaveThreads = true;
}

void TaskScheduler::StopThreads( bool bWait_ )
{
    if( m_bHaveThreads )
    {
        // wait for them threads quit before deleting data
        m_bRunning = 0;
        while( bWait_ && m_NumInternalTaskThreadsRunning )
        {
            // keep firing event to ensure all threads pick up state of m_bRunning
           WakeThreadsForNewTasks();
        }

        // detach threads starting with thread 1 (as 0 is initialization thread).
        for( uint32_t thread = m_Config.numExternalTaskThreads + 1; thread < m_NumThreads; ++thread )
        {
            assert( m_pThreads[thread] );
            m_pThreads[thread]->detach();
            delete m_pThreads[thread];
        }

        m_NumThreads = 0;
        delete[] m_pThreadDataStore;
        delete[] m_pThreads;
        m_pThreadDataStore = 0;
        m_pThreads = 0;

        SemaphoreDelete( m_pNewTaskSemaphore );
        m_pNewTaskSemaphore = 0;
        SemaphoreDelete( m_pTaskCompleteSemaphore );
        m_pTaskCompleteSemaphore = 0;

        m_bHaveThreads = false;
        m_NumThreadsWaitingForNewTasks = 0;
        m_NumThreadsWaitingForTaskCompletion = 0;
        m_NumInternalTaskThreadsRunning = 0;
        m_NumExternalTaskThreadsRegistered = 0;

        for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
        {
            delete[] m_pPipesPerThread[ priority ];
            m_pPipesPerThread[ priority ] = NULL;
            delete[] m_pPinnedTaskListPerThread[ priority ];
            m_pPinnedTaskListPerThread[ priority ] = NULL;
        }
    }
}

bool TaskScheduler::TryRunTask( uint32_t threadNum_, uint32_t& hintPipeToCheck_io_ )
{
    for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
    {
        if( TryRunTask( threadNum_, priority, hintPipeToCheck_io_ ) )
        {
            return true;
        }
    }
    return false;
}

bool TaskScheduler::TryRunTask( uint32_t threadNum_, uint32_t priority_, uint32_t& hintPipeToCheck_io_ )
{
    // Run any tasks for this thread
    RunPinnedTasks( threadNum_, priority_ );

    // check for tasks
    SubTaskSet subTask;
    bool bHaveTask = m_pPipesPerThread[ priority_ ][ threadNum_ ].WriterTryReadFront( &subTask );

    uint32_t threadToCheck = hintPipeToCheck_io_;
    uint32_t checkCount = 0;
    while( !bHaveTask && checkCount < m_NumThreads )
    {
        threadToCheck = ( hintPipeToCheck_io_ + checkCount ) % m_NumThreads;
        if( threadToCheck != threadNum_ )
        {
            bHaveTask = m_pPipesPerThread[ priority_ ][ threadToCheck ].ReaderTryReadBack( &subTask );
        }
        ++checkCount;
    }
        
    if( bHaveTask )
    {
        // update hint, will preserve value unless actually got task from another thread.
        hintPipeToCheck_io_ = threadToCheck;

        uint32_t partitionSize = subTask.partition.end - subTask.partition.start;
        if( subTask.pTask->m_RangeToRun < partitionSize )
        {
            SubTaskSet taskToRun = SplitTask( subTask, subTask.pTask->m_RangeToRun );
            SplitAndAddTask( threadNum_, subTask, subTask.pTask->m_RangeToRun );
            taskToRun.pTask->ExecuteRange( taskToRun.partition, threadNum_ );
            int prevCount = taskToRun.pTask->m_RunningCount.fetch_sub(1,std::memory_order_release );
            if( 1 == prevCount  &&
                taskToRun.pTask->m_WaitingForTaskCount.load( std::memory_order_acquire ) )
            {
                WakeThreadsForTaskCompletion();
            }
        }
        else
        {
            // the task has already been divided up by AddTaskSetToPipe, so just run it
            subTask.pTask->ExecuteRange( subTask.partition, threadNum_ );
            int prevCount = subTask.pTask->m_RunningCount.fetch_sub(1,std::memory_order_release );
            if( 1 == prevCount && 
                subTask.pTask->m_WaitingForTaskCount.load( std::memory_order_acquire ) )
            {
                WakeThreadsForTaskCompletion();
            }
        }
    }

    return bHaveTask;

}

bool TaskScheduler::HaveTasks(  uint32_t threadNum_ )
{
    for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
    {
        for( uint32_t thread = 0; thread < m_NumThreads; ++thread )
        {
            if( !m_pPipesPerThread[ priority ][ thread ].IsPipeEmpty() )
            {
                return true;
            }
        }
        if( !m_pPinnedTaskListPerThread[ priority ][ threadNum_ ].IsListEmpty() )
        {
            return true;
        }
    }
    return false;
}

void TaskScheduler::WaitForNewTasks( uint32_t threadNum_ )
{
    // We incrememt the number of threads waiting here in order
    // to ensure that the check for tasks occurs after the increment
    // to prevent a task being added after a check, then the thread waiting.
    // This will occasionally result in threads being mistakenly awoken,
    // but they will then go back to sleep.

    bool bHaveTasks = HaveTasks( threadNum_ );
    if( !bHaveTasks )
    {
        SafeCallback( m_Config.profilerCallbacks.waitForNewTaskSuspendStart, threadNum_ );
        ThreadState prevThreadState = m_pThreadDataStore[threadNum_].threadState.load( std::memory_order_relaxed );
        m_pThreadDataStore[threadNum_].threadState.store( THREAD_STATE_WAIT_NEW_TASKS, std::memory_order_relaxed ); // rely on fetch_add acquire for order
        m_NumThreadsWaitingForNewTasks.fetch_add( 1, std::memory_order_acquire );
        SemaphoreWait( *m_pNewTaskSemaphore );
        m_pThreadDataStore[threadNum_].threadState.store( prevThreadState, std::memory_order_release );
        SafeCallback( m_Config.profilerCallbacks.waitForNewTaskSuspendStop, threadNum_ );
    }
}

void TaskScheduler::WaitForTaskCompletion( const ICompletable* pCompletable_, uint32_t threadNum_ )
{
    m_NumThreadsWaitingForTaskCompletion.fetch_add( 1, std::memory_order_acquire );
    pCompletable_->m_WaitingForTaskCount.fetch_add( 1, std::memory_order_acquire );

    if( pCompletable_->GetIsComplete() )
    {
        m_NumThreadsWaitingForTaskCompletion.fetch_sub( 1, std::memory_order_release );
    }
    else
    {
        SafeCallback( m_Config.profilerCallbacks.waitForTaskCompleteSuspendStart, threadNum_ );
        ThreadState prevThreadState = m_pThreadDataStore[threadNum_].threadState.load( std::memory_order_relaxed );
        m_pThreadDataStore[threadNum_].threadState.store( THREAD_STATE_WAIT_TASK_COMPLETION, std::memory_order_relaxed );
        std::atomic_thread_fence(std::memory_order_acquire);

        SemaphoreWait( *m_pTaskCompleteSemaphore );
        if( !pCompletable_->GetIsComplete() )
        {
            // This thread which may not the one which was supposed to be awoken
            WakeThreadsForTaskCompletion();
        }
        m_pThreadDataStore[threadNum_].threadState.store( prevThreadState, std::memory_order_release );
        SafeCallback( m_Config.profilerCallbacks.waitForTaskCompleteSuspendStop, threadNum_ );
    }

    pCompletable_->m_WaitingForTaskCount.fetch_sub( 1, std::memory_order_release );
}

void TaskScheduler::WakeThreadsForNewTasks()
{
    int32_t waiting = m_NumThreadsWaitingForNewTasks.load( std::memory_order_relaxed );
    while( waiting && !m_NumThreadsWaitingForNewTasks.compare_exchange_weak(waiting, 0, std::memory_order_release, std::memory_order_relaxed ) ) {}

    if( waiting )
    {
        SemaphoreSignal( *m_pNewTaskSemaphore, waiting );
    }

    // We also wake tasks waiting for completion as they can run tasks
    WakeThreadsForTaskCompletion();
}

void TaskScheduler::WakeThreadsForTaskCompletion()
{
    // m_NumThreadsWaitingForTaskCompletion can go negative as this indicates that
    // we signalled more threads than the number which ended up waiting
    int32_t waiting = m_NumThreadsWaitingForTaskCompletion.load( std::memory_order_relaxed );
    while( waiting > 0 && !m_NumThreadsWaitingForTaskCompletion.compare_exchange_weak(waiting, 0, std::memory_order_release, std::memory_order_relaxed ) ) {}

    if( waiting > 0 )
    {
        SemaphoreSignal( *m_pTaskCompleteSemaphore, waiting );
    }
}

void TaskScheduler::SplitAndAddTask( uint32_t threadNum_, SubTaskSet subTask_, uint32_t rangeToSplit_ )
{
    int32_t numAdded = 0;
    int32_t numRun   = 0;
    // ensure that an artificial completion is not registered whilst adding tasks by incrementing count
    subTask_.pTask->m_RunningCount.fetch_add( 1, std::memory_order_acquire );
    while( subTask_.partition.start != subTask_.partition.end )
    {
        SubTaskSet taskToAdd = SplitTask( subTask_, rangeToSplit_ );

        // add the partition to the pipe
        ++numAdded;
        subTask_.pTask->m_RunningCount.fetch_add( 1, std::memory_order_acquire );
        if( !m_pPipesPerThread[ subTask_.pTask->m_Priority ][ threadNum_ ].WriterTryWriteFront( taskToAdd ) )
        {
            if( numAdded > 1 )
            {
                WakeThreadsForNewTasks();
            }
            numAdded = 0;
            // alter range to run the appropriate fraction
            if( taskToAdd.pTask->m_RangeToRun < rangeToSplit_ )
            {
                taskToAdd.partition.end = taskToAdd.partition.start + taskToAdd.pTask->m_RangeToRun;
                subTask_.partition.start = taskToAdd.partition.end;
            }
            taskToAdd.pTask->ExecuteRange( taskToAdd.partition, threadNum_ );
            ++numRun;
        }
    }
    subTask_.pTask->m_RunningCount.fetch_sub( numRun + 1, std::memory_order_release );

    // WakeThreadsForNewTasks also calls WakeThreadsForTaskCompletion() so do not need to do so above
    WakeThreadsForNewTasks();
}

ENKITS_API TaskSchedulerConfig enki::TaskScheduler::GetConfig() const
{
    TaskSchedulerConfig config;
    if( m_bHaveThreads )
    {
        config.numTaskThreadsToCreate = m_NumThreads;
        config.numExternalTaskThreads = 0;
    }
    return config;
}

void    TaskScheduler::AddTaskSetToPipe( ITaskSet* pTaskSet_ )
{
    assert( pTaskSet_->m_RunningCount == 0 );
    uint32_t threadNum = gtl_threadNum;

    ThreadState prevThreadState = m_pThreadDataStore[threadNum].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum].threadState.store( THREAD_STATE_RUNNING, std::memory_order_relaxed );
    pTaskSet_->m_RunningCount.store( 0, std::memory_order_relaxed );
    std::atomic_thread_fence(std::memory_order_acquire);


    // divide task up and add to pipe
    pTaskSet_->m_RangeToRun = pTaskSet_->m_SetSize / m_NumPartitions;
    if( pTaskSet_->m_RangeToRun < pTaskSet_->m_MinRange ) { pTaskSet_->m_RangeToRun = pTaskSet_->m_MinRange; }

    uint32_t rangeToSplit = pTaskSet_->m_SetSize / m_NumInitialPartitions;
    if( rangeToSplit < pTaskSet_->m_MinRange ) { rangeToSplit = pTaskSet_->m_MinRange; }

    SubTaskSet subTask;
    subTask.pTask = pTaskSet_;
    subTask.partition.start = 0;
    subTask.partition.end = pTaskSet_->m_SetSize;
    SplitAndAddTask( threadNum, subTask, rangeToSplit );

    m_pThreadDataStore[threadNum].threadState.store( prevThreadState, std::memory_order_release );

}

void TaskScheduler::AddPinnedTask( IPinnedTask* pTask_ )
{
    assert( pTask_->m_RunningCount == 0 );

    pTask_->m_RunningCount = 1;
    m_pPinnedTaskListPerThread[ pTask_->m_Priority ][ pTask_->threadNum ].WriterWriteFront( pTask_ );
    WakeThreadsForNewTasks();
}

void TaskScheduler::RunPinnedTasks()
{
    uint32_t threadNum = gtl_threadNum;
    ThreadState prevThreadState = m_pThreadDataStore[threadNum].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum].threadState.store( THREAD_STATE_RUNNING, std::memory_order_relaxed );
    std::atomic_thread_fence(std::memory_order_acquire);
    for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
    {
        RunPinnedTasks( threadNum, priority );
    }
    m_pThreadDataStore[threadNum].threadState.store( prevThreadState, std::memory_order_release );
}

void TaskScheduler::RunPinnedTasks( uint32_t threadNum_, uint32_t priority_ )
{
    IPinnedTask* pPinnedTaskSet = NULL;
    do
    {
        pPinnedTaskSet = m_pPinnedTaskListPerThread[ priority_ ][ threadNum_ ].ReaderReadBack();
        if( pPinnedTaskSet )
        {
            pPinnedTaskSet->Execute();
            pPinnedTaskSet->m_RunningCount = 0;
            if( pPinnedTaskSet->m_WaitingForTaskCount.load( std::memory_order_acquire ) )
            {
                WakeThreadsForTaskCompletion();
            }
        }
    } while( pPinnedTaskSet );
}

void    TaskScheduler::WaitforTask( const ICompletable* pCompletable_, enki::TaskPriority priorityOfLowestToRun_ )
{
    uint32_t threadNum = gtl_threadNum;
    uint32_t hintPipeToCheck_io = threadNum + 1;    // does not need to be clamped.

    // waiting for a task is equivalent to 'running' for thread state purpose as we may run tasks whilst waiting
    ThreadState prevThreadState = m_pThreadDataStore[threadNum].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum].threadState.store( THREAD_STATE_RUNNING, std::memory_order_relaxed );
    std::atomic_thread_fence(std::memory_order_acquire);


    if( pCompletable_ && !pCompletable_->GetIsComplete() )
    {
        SafeCallback( m_Config.profilerCallbacks.waitForTaskCompleteStart, threadNum );
        // We need to ensure that the task we're waiting on can complete even if we're the only thread,
        // so we clamp the priorityOfLowestToRun_ to no smaller than the task we're waiting for
        priorityOfLowestToRun_ = std::max( priorityOfLowestToRun_, pCompletable_->m_Priority );
        uint32_t spinCount = 0;
        while( !pCompletable_->GetIsComplete() )
        {
            ++spinCount;
            for( int priority = 0; priority <= priorityOfLowestToRun_; ++priority )
            {
                if( TryRunTask( threadNum, priority, hintPipeToCheck_io ) )
                {
                    spinCount = 0; // reset spin as ran a task
                    break;
                }
                if( spinCount > SPIN_COUNT )
                {
                    WaitForTaskCompletion( pCompletable_, threadNum );
                    spinCount = 0;
                }
                else
                {
                    uint32_t spinBackoffCount = spinCount * SPIN_BACKOFF_MULTIPLIER;
                    SpinWait( spinBackoffCount );
                }
            }

        }
        SafeCallback( m_Config.profilerCallbacks.waitForTaskCompleteStop, threadNum );
    }
    else
    {
            for( int priority = 0; priority <= priorityOfLowestToRun_; ++priority )
            {
                if( TryRunTask( gtl_threadNum, priority, hintPipeToCheck_io ) )
                {
                    break;
                }
            }
    }

    m_pThreadDataStore[threadNum].threadState.store( prevThreadState, std::memory_order_release );

}

class TaskSchedulerWaitTask : public IPinnedTask
{
    void Execute() override
    {
        // do nothing
    }
};

void TaskScheduler::WaitforAll()
{
    bool bHaveTasks = true;
    uint32_t threadNum = gtl_threadNum;
    uint32_t hintPipeToCheck_io = threadNum  + 1;    // does not need to be clamped.
    int32_t numOtherThreadsRunning = 0; // account for this thread
    uint32_t spinCount = 0;
    TaskSchedulerWaitTask dummyWaitTask;
    dummyWaitTask.threadNum = 0;
    while( bHaveTasks || numOtherThreadsRunning )
    {
        bHaveTasks = TryRunTask( threadNum, hintPipeToCheck_io );
        if( bHaveTasks )
        {
            spinCount = 0; // reset spin as ran a task
        }
        if( spinCount > SPIN_COUNT )
        {
            // find a running thread and add a dummy wait task
            int32_t countThreadsToCheck = m_NumThreads - 1;
            do
            {
                --countThreadsToCheck;
                dummyWaitTask.threadNum = ( dummyWaitTask.threadNum + 1 ) % m_NumThreads;
                if( dummyWaitTask.threadNum == threadNum )
                {
                    dummyWaitTask.threadNum = ( dummyWaitTask.threadNum + 1 ) % m_NumThreads;
                }
            } while( countThreadsToCheck && m_pThreadDataStore[ dummyWaitTask.threadNum ].threadState != THREAD_STATE_RUNNING );
            assert( dummyWaitTask.threadNum != threadNum );
            AddPinnedTask( &dummyWaitTask );
            WaitForTaskCompletion( &dummyWaitTask, threadNum );
            spinCount = 0;
        }
        else
        {
            uint32_t spinBackoffCount = spinCount * SPIN_BACKOFF_MULTIPLIER;
            SpinWait( spinBackoffCount );
        }

        // count threads running
        numOtherThreadsRunning = 0;
        for(uint32_t thread = 0; thread < m_NumThreads; ++thread )
        {
            // ignore our thread
            if( thread != threadNum )
            {
                switch( m_pThreadDataStore[thread].threadState.load( std::memory_order_acquire ) )
                {
                case THREAD_STATE_RUNNING:
                case THREAD_STATE_WAIT_TASK_COMPLETION:
                    ++numOtherThreadsRunning;
                    break;
                case THREAD_STATE_EXTERNAL_REGISTERED:
                case THREAD_STATE_EXTERNAL_UNREGISTERED:
                case THREAD_STATE_WAIT_NEW_TASKS:
                case THREAD_STATE_STOPPED:
                    break;
                 };
            }
        }
     }
}

void    TaskScheduler::WaitforAllAndShutdown()
{
    if( m_bHaveThreads )
    {
        WaitforAll();
        StopThreads(true);
    }
}

uint32_t        TaskScheduler::GetNumTaskThreads() const
{
    return m_NumThreads;
}


uint32_t TaskScheduler::GetThreadNum() const
{
    return gtl_threadNum;
}


TaskScheduler::TaskScheduler()
        : m_pPipesPerThread()
        , m_pPinnedTaskListPerThread()
        , m_NumThreads(0)
        , m_pThreadDataStore(NULL)
        , m_pThreads(NULL)
        , m_bRunning(0)
        , m_NumInternalTaskThreadsRunning(0)
        , m_NumThreadsWaitingForNewTasks(0)
        , m_NumThreadsWaitingForTaskCompletion(0)
        , m_NumPartitions(0)
        , m_bHaveThreads(false)
        , m_NumExternalTaskThreadsRegistered(0)
{
}

TaskScheduler::~TaskScheduler()
{
    StopThreads( true ); // Stops threads, waiting for them.
}

void TaskScheduler::Initialize( uint32_t numThreadsTotal_ )
{
    assert( numThreadsTotal_ >= 1 );
    m_Config.numTaskThreadsToCreate = numThreadsTotal_ - 1;
    m_Config.numExternalTaskThreads = 0;
    StopThreads( true ); // Stops threads, waiting for them.
    StartThreads();}

ENKITS_API void enki::TaskScheduler::Initialize( TaskSchedulerConfig config_ )
{
    m_Config = config_;
    StopThreads( true ); // Stops threads, waiting for them.
    StartThreads();
}

void TaskScheduler::Initialize()
{
    Initialize( std::thread::hardware_concurrency() );
}

// Semaphore implementation
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace enki
{
    struct semaphoreid_t
    {
        HANDLE      sem;
    };
    
    inline void SemaphoreCreate( semaphoreid_t& semaphoreid )
    {
        semaphoreid.sem = CreateSemaphore(NULL, 0, MAXLONG, NULL );
    }

    inline void SemaphoreClose( semaphoreid_t& semaphoreid )
    {
        CloseHandle( semaphoreid.sem );
    }

    inline void SemaphoreWait( semaphoreid_t& semaphoreid  )
    {
        DWORD retval = WaitForSingleObject( semaphoreid.sem, INFINITE );

        assert( retval != WAIT_FAILED );
    }

    inline void SemaphoreSignal( semaphoreid_t& semaphoreid, int32_t countWaiting )
    {
        if( countWaiting )
        {
            ReleaseSemaphore( semaphoreid.sem, countWaiting, NULL );
        }
    }
}
#elif defined(__MACH__)

// OS X does not have POSIX semaphores
// see https://developer.apple.com/library/content/documentation/Darwin/Conceptual/KernelProgramming/synchronization/synchronization.html
#include <mach/mach.h>

namespace enki
{
    
    struct semaphoreid_t
    {
        semaphore_t   sem;
    };
    
    inline void SemaphoreCreate( semaphoreid_t& semaphoreid )
    {
        semaphore_create( mach_task_self(), &semaphoreid.sem, SYNC_POLICY_FIFO, 0 );
    }
    
    inline void SemaphoreClose( semaphoreid_t& semaphoreid )
    {
        semaphore_destroy( mach_task_self(), semaphoreid.sem );
    }
    
    inline void SemaphoreWait( semaphoreid_t& semaphoreid  )
    {
        semaphore_wait( semaphoreid.sem );
    }
    
    inline void SemaphoreSignal( semaphoreid_t& semaphoreid, int32_t countWaiting )
    {
        while( countWaiting-- > 0 )
        {
            semaphore_signal( semaphoreid.sem );
        }
    }
}

#else // POSIX

#include <semaphore.h>
#include <errno.h>

namespace enki
{
    
    struct semaphoreid_t
    {
        sem_t   sem;
    };
    
    inline void SemaphoreCreate( semaphoreid_t& semaphoreid )
    {
        int err = sem_init( &semaphoreid.sem, 0, 0 );
        assert( err == 0 );
    }
    
    inline void SemaphoreClose( semaphoreid_t& semaphoreid )
    {
        sem_destroy( &semaphoreid.sem );
    }
    
    inline void SemaphoreWait( semaphoreid_t& semaphoreid  )
    {
        while( sem_wait( &semaphoreid.sem ) == -1 && errno == EINTR ) {}
    }
    
    inline void SemaphoreSignal( semaphoreid_t& semaphoreid, int32_t countWaiting )
    {
        while( countWaiting-- > 0 )
        {
            sem_post( &semaphoreid.sem );
        }
    }
}
#endif

namespace enki
{
    semaphoreid_t* SemaphoreCreate()
    {
        semaphoreid_t* pSemaphore = new semaphoreid_t;
        SemaphoreCreate( *pSemaphore );
        return pSemaphore;
    }

    void SemaphoreDelete( semaphoreid_t* pSemaphore_ )
    {
        SemaphoreClose( *pSemaphore_ );
        delete pSemaphore_;
    }
}