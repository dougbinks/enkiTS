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

#if defined __i386__ || defined __x86_64__
#include "x86intrin.h"
#elif defined _WIN32
#include <intrin.h>
#endif

using namespace enki;


static const uint32_t PIPESIZE_LOG2              = 8;
static const uint32_t SPIN_COUNT                 = 100;
static const uint32_t SPIN_BACKOFF_MULTIPLIER    = 10;
static const uint32_t MAX_NUM_INITIAL_PARTITIONS = 8;

// each software thread gets it's own copy of gtl_threadNum, so this is safe to use as a static variable
static THREAD_LOCAL uint32_t                             gtl_threadNum       = 0;

namespace enki 
{
    struct SubTaskSet
    {
        ITaskSet*           pTask;
        TaskSetPartition    partition;
    };

    // we derive class TaskPipe rather than typedef to get forward declaration working easily
    class TaskPipe : public LockLessMultiReadPipe<PIPESIZE_LOG2,enki::SubTaskSet> {};

    struct ThreadArgs
    {
        uint32_t        threadNum;
        TaskScheduler*  pTaskScheduler;
    };

    class PinnedTaskList : public LocklessMultiWriteIntrusiveList<IPinnedTask> {};
}

namespace
{
    SubTaskSet       SplitTask( SubTaskSet& subTask_, uint32_t rangeToSplit_ )
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


static void SafeCallback(ProfilerCallbackFunc func_, uint32_t threadnum_)
{
    if( func_ )
    {
        func_(threadnum_);
    }
}

ProfilerCallbacks* TaskScheduler::GetProfilerCallbacks()
{
    return &m_ProfilerCallbacks;
}

THREADFUNC_DECL TaskScheduler::TaskingThreadFunction( void* pArgs )
{
    ThreadArgs args                    = *(ThreadArgs*)pArgs;
    uint32_t threadNum                = args.threadNum;
    TaskScheduler*  pTS                = args.pTaskScheduler;
    gtl_threadNum      = threadNum;

    SafeCallback( pTS->m_ProfilerCallbacks.threadStart, threadNum );
    
    uint32_t spinCount = SPIN_COUNT + 1;
    uint32_t hintPipeToCheck_io = threadNum + 1;    // does not need to be clamped.
    while( pTS->m_bRunning )
    {
        if(!pTS->TryRunTask( threadNum, hintPipeToCheck_io ) )
        {
            // no tasks, will spin then wait
            ++spinCount;
            if( spinCount > SPIN_COUNT )
            {
                pTS->WaitForTasks( threadNum );
                spinCount = 0;
            }
            else
            {
                // Note: see https://software.intel.com/en-us/articles/a-common-construct-to-avoid-the-contention-of-threads-architecture-agnostic-spin-wait-loops
                uint32_t spinBackoffCount = spinCount * SPIN_BACKOFF_MULTIPLIER;
                SpinWait( spinBackoffCount );
            }
        }
        else
        {
            spinCount = 0;
        }
    }

    AtomicAdd( &pTS->m_NumThreadsRunning, -1 );
    SafeCallback( pTS->m_ProfilerCallbacks.threadStop, threadNum );

    return 0;
}


void TaskScheduler::StartThreads()
{
    if( m_bHaveThreads )
    {
        return;
    }

    for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
    {
        m_pPipesPerThread[ priority ]          = new TaskPipe[ m_NumThreads ];
        m_pPinnedTaskListPerThread[ priority ] = new PinnedTaskList[ m_NumThreads ];
    }
    

    SemaphoreCreate( m_NewTaskSemaphore );

    // we create one less thread than m_NumThreads as the main thread counts as one
    m_pThreadArgStore = new ThreadArgs[m_NumThreads];
    m_pThreadIDs      = new threadid_t[m_NumThreads];
    m_pThreadArgStore[0].threadNum      = 0;
    m_pThreadArgStore[0].pTaskScheduler = this;
    m_pThreadIDs[0] = 0;
    m_NumThreadsWaiting = 0;
    m_NumThreadsRunning = 1;// acount for main thread
    m_bRunning = true;

    for( uint32_t thread = 1; thread < m_NumThreads; ++thread )
    {
        m_pThreadArgStore[thread].threadNum      = thread;
        m_pThreadArgStore[thread].pTaskScheduler = this;
        ThreadCreate( &m_pThreadIDs[thread], TaskingThreadFunction, &m_pThreadArgStore[thread] );
        ++m_NumThreadsRunning;
    }

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
        m_NumPartitions = m_NumThreads * (m_NumThreads - 1);
        m_NumInitialPartitions = m_NumThreads - 1;
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
        m_bRunning = false;
        while( bWait_ && m_NumThreadsRunning > 1 )
        {
            // keep firing event to ensure all threads pick up state of m_bRunning
            SemaphoreSignal( m_NewTaskSemaphore, m_NumThreadsRunning );
        }

        for( uint32_t thread = 1; thread < m_NumThreads; ++thread )
        {
            ThreadTerminate( m_pThreadIDs[thread] );
        }

        m_NumThreads = 0;
        delete[] m_pThreadArgStore;
        delete[] m_pThreadIDs;
        m_pThreadArgStore = 0;
        m_pThreadIDs = 0;
        SemaphoreClose( m_NewTaskSemaphore );

        m_bHaveThreads = false;
        m_NumThreadsWaiting = 0;
        m_NumThreadsRunning = 0;

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
            AtomicAdd( &taskToRun.pTask->m_RunningCount, -1 );
        }
        else
        {

            // the task has already been divided up by AddTaskSetToPipe, so just run it
            subTask.pTask->ExecuteRange( subTask.partition, threadNum_ );
            AtomicAdd( &subTask.pTask->m_RunningCount, -1 );
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


void TaskScheduler::WaitForTasks( uint32_t threadNum )
{
    // We incrememt the number of threads waiting here in order
    // to ensure that the check for tasks occurs after the increment
    // to prevent a task being added after a check, then the thread waiting.
    // This will occasionally result in threads being mistakenly awoken,
    // but they will then go back to sleep.
    AtomicAdd( &m_NumThreadsWaiting, 1 );

    bool bHaveTasks = HaveTasks( threadNum );
    if( !bHaveTasks )
    {
        SafeCallback( m_ProfilerCallbacks.waitStart, threadNum );
        SemaphoreWait( m_NewTaskSemaphore );
        SafeCallback( m_ProfilerCallbacks.waitStop, threadNum );
    }

    int32_t prev = AtomicAdd( &m_NumThreadsWaiting, -1 );
    assert( prev != 0 );
}

void TaskScheduler::WakeThreads(  int32_t maxToWake_ )
{
    if( maxToWake_ > 0 && maxToWake_  < m_NumThreadsWaiting )
    {
        SemaphoreSignal( m_NewTaskSemaphore, maxToWake_ );
    }
    else
    {
        SemaphoreSignal( m_NewTaskSemaphore, m_NumThreadsWaiting );
    }
}

void TaskScheduler::SplitAndAddTask( uint32_t threadNum_, SubTaskSet subTask_, uint32_t rangeToSplit_ )
{
    while( subTask_.partition.start != subTask_.partition.end )
    {
        SubTaskSet taskToAdd = SplitTask( subTask_, rangeToSplit_ );

        // add the partition to the pipe
        AtomicAdd( &subTask_.pTask->m_RunningCount, 1 );
        if( !m_pPipesPerThread[ subTask_.pTask->m_Priority ][ threadNum_ ].WriterTryWriteFront( taskToAdd ) )
        {

            // alter range to run the appropriate fraction
            if( taskToAdd.pTask->m_RangeToRun < rangeToSplit_ )
            {
                taskToAdd.partition.end = taskToAdd.partition.start + taskToAdd.pTask->m_RangeToRun;
                subTask_.partition.start = taskToAdd.partition.end;
            }
            taskToAdd.pTask->ExecuteRange( taskToAdd.partition, threadNum_ );
            AtomicAdd( &subTask_.pTask->m_RunningCount, -1 );
        }
        else
        {
            WakeThreads( 1 );
        }
    }

}

void    TaskScheduler::AddTaskSetToPipe( ITaskSet* pTaskSet )
{
    pTaskSet->m_RunningCount = 0;

    // divide task up and add to pipe
    pTaskSet->m_RangeToRun = pTaskSet->m_SetSize / m_NumPartitions;
    if( pTaskSet->m_RangeToRun < pTaskSet->m_MinRange ) { pTaskSet->m_RangeToRun = pTaskSet->m_MinRange; }

    uint32_t rangeToSplit = pTaskSet->m_SetSize / m_NumInitialPartitions;
    if( rangeToSplit < pTaskSet->m_MinRange ) { rangeToSplit = pTaskSet->m_MinRange; }

    SubTaskSet subTask;
    subTask.pTask = pTaskSet;
    subTask.partition.start = 0;
    subTask.partition.end = pTaskSet->m_SetSize;
    SplitAndAddTask( gtl_threadNum, subTask, rangeToSplit );
}

void TaskScheduler::AddPinnedTask( IPinnedTask* pTask_ )
{
    pTask_->m_RunningCount = 1;
    m_pPinnedTaskListPerThread[ pTask_->m_Priority ][ pTask_->threadNum ].WriterWriteFront( pTask_ );
    WakeThreads();
}

void TaskScheduler::RunPinnedTasks()
{
    uint32_t threadNum = gtl_threadNum;
    for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
    {
        RunPinnedTasks( threadNum, priority );
    }
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
        }
    } while( pPinnedTaskSet );
}

void    TaskScheduler::WaitforTask( const ICompletable* pCompletable_, enki::TaskPriority priorityOfLowestToRun_ )
{
    uint32_t hintPipeToCheck_io = gtl_threadNum + 1;    // does not need to be clamped.
    if( pCompletable_ )
    {
        while( pCompletable_->m_RunningCount )
        {
            for( int priority = 0; priority <= priorityOfLowestToRun_; ++priority )
            {
                if( TryRunTask( gtl_threadNum, priority, hintPipeToCheck_io ) )
                {
                    break;
                }
            }
        }
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
}

void    TaskScheduler::WaitforAll()
{
    bool bHaveTasks = true;
    uint32_t hintPipeToCheck_io = gtl_threadNum  + 1;    // does not need to be clamped.
    int32_t threadsRunning = m_NumThreadsRunning - 1;
    while( bHaveTasks || m_NumThreadsWaiting < threadsRunning )
    {
        bHaveTasks = TryRunTask( gtl_threadNum, hintPipeToCheck_io );
    }
}

void    TaskScheduler::WaitforAllAndShutdown()
{
    WaitforAll();
    StopThreads(true);
}

uint32_t        TaskScheduler::GetNumTaskThreads() const
{
    return m_NumThreads;
}

TaskScheduler::TaskScheduler()
        : m_pPipesPerThread()
        , m_pPinnedTaskListPerThread()
        , m_NumThreads(0)
        , m_pThreadArgStore(NULL)
        , m_pThreadIDs(NULL)
        , m_bRunning(false)
        , m_NumThreadsRunning(0)
        , m_NumThreadsWaiting(0)
        , m_NumPartitions(0)
        , m_bHaveThreads(false)
{
    memset(&m_ProfilerCallbacks, 0, sizeof(m_ProfilerCallbacks));
}

TaskScheduler::~TaskScheduler()
{
    StopThreads( true ); // Stops threads, waiting for them.
}

void    TaskScheduler::Initialize( uint32_t numThreads_ )
{
    assert( numThreads_ );
    StopThreads( true ); // Stops threads, waiting for them.

    m_NumThreads = numThreads_;

    StartThreads();
}

void   TaskScheduler::Initialize()
{
    Initialize( GetNumHardwareThreads() );
}