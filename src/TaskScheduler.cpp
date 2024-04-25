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

#include "TaskScheduler.h"
#include "LockLessMultiReadPipe.h"

#include <algorithm>

#if defined __i386__ || defined __x86_64__
#include "x86intrin.h"
#elif defined _WIN32
#include <intrin.h>
#endif

using namespace enki;

#if defined(ENKI_CUSTOM_ALLOC_FILE_AND_LINE)
#define ENKI_FILE_AND_LINE __FILE__, __LINE__
#else
namespace
{
    const char* gc_File    = "";
    const uint32_t gc_Line = 0;
}
#define ENKI_FILE_AND_LINE  gc_File, gc_Line
#endif

// UWP and MinGW don't have GetActiveProcessorCount
#if defined(_WIN64) \
    && !defined(__MINGW32__) \
    && !(defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_PC_APP || WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP))
#define ENKI_USE_WINDOWS_PROCESSOR_API
#endif

#ifdef ENKI_USE_WINDOWS_PROCESSOR_API
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>
#endif

uint32_t enki::GetNumHardwareThreads()
{
#ifdef ENKI_USE_WINDOWS_PROCESSOR_API
    return GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
    return std::thread::hardware_concurrency();
#endif
}

namespace enki
{
    static constexpr int32_t  gc_TaskStartCount          = 2;
    static constexpr int32_t  gc_TaskAlmostCompleteCount = 1; // GetIsComplete() will return false, but execution is done and about to complete
    static constexpr uint32_t gc_PipeSizeLog2            = 8;
    static constexpr uint32_t gc_SpinCount               = 10;
    static constexpr uint32_t gc_SpinBackOffMultiplier   = 100;
    static constexpr uint32_t gc_MaxNumInitialPartitions = 8;
    static constexpr uint32_t gc_MaxStolenPartitions     = 1 << gc_PipeSizeLog2;
    static constexpr uint32_t gc_CacheLineSize           = 64;
    // awaiting std::hardware_constructive_interference_size
}

// thread_local not well supported yet by some older C++11 compilers.
// For XCode before version 8 thread_local is not defined, so add to your compile defines: ENKI_THREAD_LOCAL __thread
#ifndef ENKI_THREAD_LOCAL
#if defined(_MSC_VER) && _MSC_VER <= 1800
        #define ENKI_THREAD_LOCAL __declspec(thread)
// Removed below as XCode supports thread_local since version 8
// #elif __APPLE__
//         // Apple thread_local currently not implemented in XCode before version 8 despite it being in Clang.
//         #define ENKI_THREAD_LOCAL __thread
#else
        #define ENKI_THREAD_LOCAL thread_local
#endif
#endif


// each software thread gets its own copy of gtl_threadNum, so this is safe to use as a static variable
static ENKI_THREAD_LOCAL uint32_t  gtl_threadNum             = enki::NO_THREAD_NUM;

namespace enki
{
    struct SubTaskSet
    {
        ITaskSet*           pTask;
        TaskSetPartition    partition;
    };

    // we derive class TaskPipe rather than typedef to get forward declaration working easily
    class TaskPipe : public LockLessMultiReadPipe<gc_PipeSizeLog2, enki::SubTaskSet> {};

    enum ThreadState : int32_t
    {
        ENKI_THREAD_STATE_NONE,                  // shouldn't get this value
        ENKI_THREAD_STATE_NOT_LAUNCHED,          // for debug purposes - indicates enki task thread not yet launched
        ENKI_THREAD_STATE_RUNNING,
        ENKI_THREAD_STATE_PRIMARY_REGISTERED,    // primary thread is the one enkiTS was initialized on
        ENKI_THREAD_STATE_EXTERNAL_REGISTERED,
        ENKI_THREAD_STATE_EXTERNAL_UNREGISTERED,
        ENKI_THREAD_STATE_WAIT_TASK_COMPLETION,
        ENKI_THREAD_STATE_WAIT_NEW_TASKS,
        ENKI_THREAD_STATE_WAIT_NEW_PINNED_TASKS,
        ENKI_THREAD_STATE_STOPPED,
    };

    struct ThreadArgs
    {
        uint32_t                 threadNum;
        TaskScheduler*           pTaskScheduler;
    };

    struct alignas(enki::gc_CacheLineSize) ThreadDataStore
    {
        semaphoreid_t*           pWaitNewPinnedTaskSemaphore = nullptr;
        std::atomic<ThreadState> threadState = { ENKI_THREAD_STATE_NONE };
        uint32_t                 rndSeed = 0;
        char prevent_false_Share[ enki::gc_CacheLineSize - sizeof(std::atomic<ThreadState>) - sizeof(semaphoreid_t*) - sizeof( uint32_t ) ]; // required to prevent alignment padding warning
    };
    constexpr size_t SIZEOFTHREADDATASTORE = sizeof( ThreadDataStore ); // for easier inspection
    static_assert( SIZEOFTHREADDATASTORE == enki::gc_CacheLineSize, "ThreadDataStore may exhibit false sharing" );

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
        rangeToSplit_ = std::min( rangeToSplit_, rangeLeft );
        splitTask.partition.end = subTask_.partition.start + rangeToSplit_;
        subTask_.partition.start = splitTask.partition.end;
        return splitTask;
    }

    #if ( defined _WIN32 && ( defined _M_IX86  || defined _M_X64 ) ) || ( defined __i386__ || defined __x86_64__ )
    // Note: see https://software.intel.com/en-us/articles/a-common-construct-to-avoid-the-contention-of-threads-architecture-agnostic-spin-wait-loops
    void SpinWait( uint32_t spinCount_ )
    {
        uint64_t end = __rdtsc() + spinCount_;
        while( __rdtsc() < end )
        {
            _mm_pause();
        }
    }
    #else
    void SpinWait( uint32_t spinCount_ )
    {
        while( spinCount_ )
        {
            // TODO: may have NOP or yield equiv
            --spinCount_;
        }
    }
    #endif

    void SafeCallback( ProfilerCallbackFunc func_, uint32_t threadnum_ )
    {
        if( func_ != nullptr )
        {
            func_( threadnum_ );
        }
    }
}


ENKITS_API void* enki::DefaultAllocFunc( size_t align_, size_t size_, void* userData_, const char* file_, int line_ )
{
    (void)userData_; (void)file_; (void)line_;
    void* pRet;
#ifdef _WIN32
    pRet = (void*)_aligned_malloc( size_, align_ );
#else
    pRet = nullptr;
    if( align_ <= size_ && align_ <= alignof(int64_t) )
    {
        // no need for alignment, use malloc
        pRet = malloc( size_ );
    }
    else
    {
        int retval = posix_memalign( &pRet, align_, size_ );
        (void)retval; // unused
    }
#endif
    return pRet;
}

ENKITS_API void  enki::DefaultFreeFunc(  void* ptr_,   size_t size_, void* userData_, const char* file_, int line_ )
{
     (void)size_; (void)userData_; (void)file_; (void)line_;
#ifdef _WIN32
    _aligned_free( ptr_ );
#else
    free( ptr_ );
#endif
}

bool TaskScheduler::RegisterExternalTaskThread()
{
    bool bRegistered = false;
    while( !bRegistered && m_NumExternalTaskThreadsRegistered < (int32_t)m_Config.numExternalTaskThreads  )
    {
        for(uint32_t thread = GetNumFirstExternalTaskThread(); thread < GetNumFirstExternalTaskThread() + m_Config.numExternalTaskThreads; ++thread )
        {
            ThreadState threadStateExpected = ENKI_THREAD_STATE_EXTERNAL_UNREGISTERED;
            if( m_pThreadDataStore[thread].threadState.compare_exchange_strong(
                threadStateExpected, ENKI_THREAD_STATE_EXTERNAL_REGISTERED ) )
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

bool TaskScheduler::RegisterExternalTaskThread( uint32_t threadNumToRegister_ )
{
    ENKI_ASSERT( threadNumToRegister_ >= GetNumFirstExternalTaskThread() );
    ENKI_ASSERT( threadNumToRegister_ < ( GetNumFirstExternalTaskThread() + m_Config.numExternalTaskThreads ) );
    ThreadState threadStateExpected = ENKI_THREAD_STATE_EXTERNAL_UNREGISTERED;
    if( m_pThreadDataStore[threadNumToRegister_].threadState.compare_exchange_strong(
        threadStateExpected, ENKI_THREAD_STATE_EXTERNAL_REGISTERED ) )
    {
        ++m_NumExternalTaskThreadsRegistered;
        gtl_threadNum = threadNumToRegister_;
        return true;
    }
    return false;
}


void TaskScheduler::DeRegisterExternalTaskThread()
{
    ENKI_ASSERT( gtl_threadNum != enki::NO_THREAD_NUM );
    ENKI_ASSERT( gtl_threadNum >= GetNumFirstExternalTaskThread() );
    ThreadState threadState = m_pThreadDataStore[gtl_threadNum].threadState.load( std::memory_order_acquire );
    ENKI_ASSERT( threadState == ENKI_THREAD_STATE_EXTERNAL_REGISTERED );
    if( threadState == ENKI_THREAD_STATE_EXTERNAL_REGISTERED )
    {
        --m_NumExternalTaskThreadsRegistered;
        m_pThreadDataStore[gtl_threadNum].threadState.store( ENKI_THREAD_STATE_EXTERNAL_UNREGISTERED, std::memory_order_release );
        gtl_threadNum = enki::NO_THREAD_NUM;
    }
}

uint32_t TaskScheduler::GetNumRegisteredExternalTaskThreads()
{
    return m_NumExternalTaskThreadsRegistered;
}

void TaskScheduler::TaskingThreadFunction( const ThreadArgs& args_ )
{
    uint32_t threadNum  = args_.threadNum;
    TaskScheduler*  pTS = args_.pTaskScheduler;
    gtl_threadNum       = threadNum;

    pTS->m_pThreadDataStore[threadNum].threadState.store( ENKI_THREAD_STATE_RUNNING, std::memory_order_release );
    SafeCallback( pTS->m_Config.profilerCallbacks.threadStart, threadNum );

    uint32_t spinCount = 0;
    uint32_t hintPipeToCheck_io = threadNum + 1; // does not need to be clamped.
    while( pTS->GetIsRunning() )
    {
        if( !pTS->TryRunTask( threadNum, hintPipeToCheck_io ) )
        {
            // no tasks, will spin then wait
            ++spinCount;
            if( spinCount > gc_SpinCount )
            {
                pTS->WaitForNewTasks( threadNum );
            }
            else
            {
                uint32_t spinBackoffCount = spinCount * gc_SpinBackOffMultiplier;
                SpinWait( spinBackoffCount );
            }
        }
        else
        {
            spinCount = 0; // have run a task so reset spin count.
        }
    }

    pTS->m_NumInternalTaskThreadsRunning.fetch_sub( 1, std::memory_order_release );
    pTS->m_pThreadDataStore[threadNum].threadState.store( ENKI_THREAD_STATE_STOPPED, std::memory_order_release );
    SafeCallback( pTS->m_Config.profilerCallbacks.threadStop, threadNum );
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
        m_pPipesPerThread[ priority ]          = NewArray<TaskPipe>( m_NumThreads, ENKI_FILE_AND_LINE );
        m_pPinnedTaskListPerThread[ priority ] = NewArray<PinnedTaskList>( m_NumThreads, ENKI_FILE_AND_LINE );
    }

    m_pNewTaskSemaphore      = SemaphoreNew();
    m_pTaskCompleteSemaphore = SemaphoreNew();

    // we create one less thread than m_NumThreads as the main thread counts as one
    m_pThreadDataStore   = NewArray<ThreadDataStore>( m_NumThreads, ENKI_FILE_AND_LINE );
    m_pThreads           = NewArray<std::thread>( m_NumThreads, ENKI_FILE_AND_LINE );
    m_bRunning = true;
    m_bWaitforAllCalled = false;
    m_bShutdownRequested = false;

    // current thread is primary enkiTS thread
    m_pThreadDataStore[0].threadState = ENKI_THREAD_STATE_PRIMARY_REGISTERED;
    gtl_threadNum = 0;

    for( uint32_t thread = GetNumFirstExternalTaskThread(); thread < m_Config.numExternalTaskThreads + GetNumFirstExternalTaskThread(); ++thread )
    {
        m_pThreadDataStore[thread].threadState   = ENKI_THREAD_STATE_EXTERNAL_UNREGISTERED;
    }
    for( uint32_t thread = m_Config.numExternalTaskThreads + GetNumFirstExternalTaskThread(); thread < m_NumThreads; ++thread )
    {
        m_pThreadDataStore[thread].threadState   = ENKI_THREAD_STATE_NOT_LAUNCHED;
    }


    // Create Wait New Pinned Task Semaphores and init rndSeed
    for( uint32_t threadNum = 0; threadNum < m_NumThreads; ++threadNum )
    {
        m_pThreadDataStore[threadNum].pWaitNewPinnedTaskSemaphore = SemaphoreNew();
        m_pThreadDataStore[threadNum].rndSeed = threadNum;
    }

    // only launch threads once all thread states are set
    for( uint32_t thread = m_Config.numExternalTaskThreads + GetNumFirstExternalTaskThread(); thread < m_NumThreads; ++thread )
    {
        m_pThreads[thread]                       = std::thread( TaskingThreadFunction, ThreadArgs{ thread, this } );
        ++m_NumInternalTaskThreadsRunning;
    }

    // ensure we have sufficient tasks to equally fill either all threads including main
    // or just the threads we've launched, this is outside the first init as we want to be able
    // to runtime change it
    if( 1 == m_NumThreads )
    {
        m_NumPartitions        = 1;
        m_NumInitialPartitions = 1;
    }
    else
    {
        // There could be more threads than hardware threads if external threads are
        // being intended for blocking functionality such as io etc.
        // We only need to partition for a maximum of the available processor parallelism.
        uint32_t numThreadsToPartitionFor = std::min( m_NumThreads, GetNumHardwareThreads() );
        m_NumPartitions = numThreadsToPartitionFor * (numThreadsToPartitionFor - 1);
        // ensure m_NumPartitions, m_NumInitialPartitions non zero, can happen if m_NumThreads > 1 && GetNumHardwareThreads() == 1
        m_NumPartitions        = std::max( m_NumPartitions,              (uint32_t)1 );
        m_NumInitialPartitions = std::max( numThreadsToPartitionFor - 1, (uint32_t)1 );
        m_NumInitialPartitions = std::min( m_NumInitialPartitions, gc_MaxNumInitialPartitions );
    }

#ifdef ENKI_USE_WINDOWS_PROCESSOR_API
    // x64 bit Windows may support >64 logical processors using processor groups, and only allocate threads to a default group.
    // We need to detect this and distribute threads accordingly
    if( GetNumHardwareThreads() > 64 &&                                    // only have processor groups if > 64 hardware threads
        std::thread::hardware_concurrency() < GetNumHardwareThreads() &&   // if std::thread sees > 64 hardware threads no need to distribute
        std::thread::hardware_concurrency() < m_NumThreads )               // no need to distribute if number of threads requested lower than std::thread sees
    {
        uint32_t numProcessorGroups = GetActiveProcessorGroupCount();
        GROUP_AFFINITY mainThreadAffinity;
        BOOL success = GetThreadGroupAffinity( GetCurrentThread(), &mainThreadAffinity );
        ENKI_ASSERT( success );
        if( success )
        {
            uint32_t mainProcessorGroup = mainThreadAffinity.Group;
            uint32_t currLogicalProcess = GetActiveProcessorCount( (WORD)mainProcessorGroup ); // we start iteration at end of current process group's threads

            // If more threads are created than there are logical processors then we still want to distribute them evenly amongst groups
            // so we iterate continuously around the groups until we reach m_NumThreads
            uint32_t group = 0;
            while( currLogicalProcess < m_NumThreads )
            {
                ++group; // start at group 1 since we set currLogicalProcess to start of next group
                uint32_t currGroup = ( group + mainProcessorGroup ) % numProcessorGroups; // we start at mainProcessorGroup, go round in circles
                uint32_t groupNumLogicalProcessors = GetActiveProcessorCount( (WORD)currGroup );
                ENKI_ASSERT( groupNumLogicalProcessors <= 64 );
                uint64_t GROUPMASK = 0xFFFFFFFFFFFFFFFFULL >> (64-groupNumLogicalProcessors); // group mask should not have 1's where there are no processors
                for( uint32_t groupLogicalProcess = 0; ( groupLogicalProcess < groupNumLogicalProcessors ) && ( currLogicalProcess < m_NumThreads ); ++groupLogicalProcess, ++currLogicalProcess )
                {
                    if( currLogicalProcess > m_Config.numExternalTaskThreads + GetNumFirstExternalTaskThread() )
                    {
                        auto thread_handle = m_pThreads[currLogicalProcess].native_handle();

                        // From https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups
                        // If a thread is assigned to a different group than the process, the process's affinity is updated to include the thread's affinity
                        // and the process becomes a multi-group process.
                        GROUP_AFFINITY threadAffinity;
                        success = GetThreadGroupAffinity( thread_handle, &threadAffinity );
                        ENKI_ASSERT(success); (void)success;
                        if( threadAffinity.Group != currGroup )
                        {
                            threadAffinity.Group = (WORD)currGroup;
                            threadAffinity.Mask  = GROUPMASK;
                            success = SetThreadGroupAffinity( thread_handle, &threadAffinity, nullptr );
                            ENKI_ASSERT( success ); (void)success;
                        }
                    }
                }
            }
        }
    }
#endif

    m_bHaveThreads = true;
}

void TaskScheduler::StopThreads( bool bWait_ )
{
    // we set m_bWaitforAllCalled to true to ensure any task which loop using this status exit
    m_bWaitforAllCalled.store( true, std::memory_order_release );

    // set status
    m_bShutdownRequested.store( true, std::memory_order_release );
    m_bRunning.store( false, std::memory_order_release );

    if( m_bHaveThreads )
    {

        // wait for threads to quit before deleting data
        while( bWait_ && m_NumInternalTaskThreadsRunning )
        {
            // keep firing event to ensure all threads pick up state of m_bRunning
           WakeThreadsForNewTasks();

           for( uint32_t threadId = 0; threadId < m_NumThreads; ++threadId )
           {
               // send wait for new pinned tasks signal to ensure any waiting are awoken
               SemaphoreSignal( *m_pThreadDataStore[ threadId ].pWaitNewPinnedTaskSemaphore, 1 );
           }
        }

        // detach threads starting with thread GetNumFirstExternalTaskThread() (as 0 is initialization thread).
        for( uint32_t thread = m_Config.numExternalTaskThreads +  GetNumFirstExternalTaskThread(); thread < m_NumThreads; ++thread )
        {
            ENKI_ASSERT( m_pThreads[thread].joinable() );
            m_pThreads[thread].join();
        }

        // delete any Wait New Pinned Task Semaphores
        for( uint32_t threadNum = 0; threadNum < m_NumThreads; ++threadNum )
        {
            SemaphoreDelete( m_pThreadDataStore[threadNum].pWaitNewPinnedTaskSemaphore );
        }

        DeleteArray( m_pThreadDataStore, m_NumThreads, ENKI_FILE_AND_LINE );
        DeleteArray( m_pThreads, m_NumThreads, ENKI_FILE_AND_LINE );
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
            DeleteArray( m_pPipesPerThread[ priority ], m_NumThreads, ENKI_FILE_AND_LINE );
            m_pPipesPerThread[ priority ] = NULL;
            DeleteArray( m_pPinnedTaskListPerThread[ priority ], m_NumThreads, ENKI_FILE_AND_LINE );
            m_pPinnedTaskListPerThread[ priority ] = NULL;
        }
        m_NumThreads = 0;
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

static inline uint32_t RotateLeft( uint32_t value, int32_t count )
{
    return ( value << count ) | ( value >> ( 32 - count ));
}
/*  xxHash variant based on documentation on
    https://github.com/Cyan4973/xxHash/blob/eec5700f4d62113b47ee548edbc4746f61ffb098/doc/xxhash_spec.md

    Copyright (c) Yann Collet

    Permission is granted to copy and distribute this document for any purpose and without charge, including translations into other languages and incorporation into compilations, provided that the copyright notice and this notice are preserved, and that any substantive changes or deletions from the original are clearly marked. Distribution of this document is unlimited.
*/
static inline uint32_t Hash32( uint32_t in_ )
{
    static const uint32_t PRIME32_1 = 2654435761U;  // 0b10011110001101110111100110110001
    static const uint32_t PRIME32_2 = 2246822519U;  // 0b10000101111010111100101001110111
    static const uint32_t PRIME32_3 = 3266489917U;  // 0b11000010101100101010111000111101
    static const uint32_t PRIME32_4 =  668265263U;  // 0b00100111110101001110101100101111
    static const uint32_t PRIME32_5 =  374761393U;  // 0b00010110010101100110011110110001
    static const uint32_t SEED      = 0; // can configure seed if needed

    // simple hash of nodes, does not check if nodePool is compressed or not.
    uint32_t acc = SEED + PRIME32_5;

    // add node types to map, and also ensure that fully empty nodes are well distributed by hashing the pointer.
    acc += in_;
    acc = acc ^ (acc >> 15);
    acc = acc * PRIME32_2;
    acc = acc ^ (acc >> 13);
    acc = acc * PRIME32_3;
    acc = acc ^ (acc >> 16);
    return acc;
}

bool TaskScheduler::TryRunTask( uint32_t threadNum_, uint32_t priority_, uint32_t& hintPipeToCheck_io_ )
{
    // Run any tasks for this thread
    RunPinnedTasks( threadNum_, priority_ );

    // check for tasks
    SubTaskSet subTask;
    bool bHaveTask = m_pPipesPerThread[ priority_ ][ threadNum_ ].WriterTryReadFront( &subTask );

    uint32_t threadToCheckStart = hintPipeToCheck_io_ % m_NumThreads;
    uint32_t threadToCheck      = threadToCheckStart;
    uint32_t checkCount = 0;
    if( !bHaveTask )
    {
        bHaveTask = m_pPipesPerThread[ priority_ ][ threadToCheck ].ReaderTryReadBack( &subTask );
        if( !bHaveTask )
        {
            // To prevent many threads checking the same task pipe for work we pseudorandomly distribute
            // the starting thread which we start checking for tasks to run
            uint32_t& rndSeed = m_pThreadDataStore[threadNum_].rndSeed;
            ++rndSeed;
            uint32_t threadToCheckOffset = Hash32( rndSeed * threadNum_ );
            while( !bHaveTask && checkCount < m_NumThreads )
            {
                threadToCheck = ( threadToCheckOffset + checkCount ) % m_NumThreads;
                if( threadToCheck != threadNum_ && threadToCheckOffset != threadToCheckStart )
                {
                    bHaveTask = m_pPipesPerThread[ priority_ ][ threadToCheck ].ReaderTryReadBack( &subTask );
                }
                ++checkCount;
            }
        }
    }

    if( bHaveTask )
    {
        // update hint, will preserve value unless actually got task from another thread.
        hintPipeToCheck_io_ = threadToCheck;

        uint32_t partitionSize = subTask.partition.end - subTask.partition.start;
        if( subTask.pTask->m_RangeToRun < partitionSize )
        {
            SubTaskSet taskToRun = SplitTask( subTask, subTask.pTask->m_RangeToRun );
            uint32_t rangeToSplit = subTask.pTask->m_RangeToRun;
            if( threadNum_ != threadToCheck )
            {
                // task was stolen from another thread
                // in order to ensure other threads can get enough work we need to split into larger ranges
                // these larger splits are then stolen and split themselves
                // otherwise other threads must keep stealing from this thread, which may stall when pipe is full
                rangeToSplit = std::max( rangeToSplit, (subTask.partition.end - subTask.partition.start) / gc_MaxStolenPartitions );
            }
            SplitAndAddTask( threadNum_, subTask, rangeToSplit );
            taskToRun.pTask->ExecuteRange( taskToRun.partition, threadNum_ );
            int prevCount = taskToRun.pTask->m_RunningCount.fetch_sub(1,std::memory_order_acq_rel );
            if( gc_TaskStartCount == prevCount )
            {
                TaskComplete( taskToRun.pTask, true, threadNum_ );
            }
        }
        else
        {
            // the task has already been divided up by AddTaskSetToPipe, so just run it
            subTask.pTask->ExecuteRange( subTask.partition, threadNum_ );
            int prevCount = subTask.pTask->m_RunningCount.fetch_sub(1,std::memory_order_acq_rel );
            if( gc_TaskStartCount == prevCount )
            {
                TaskComplete( subTask.pTask, true, threadNum_ );
            }
        }
    }

    return bHaveTask;

}

void TaskScheduler::TaskComplete( ICompletable* pTask_, bool bWakeThreads_, uint32_t threadNum_ )
{
    // It must be impossible for a thread to enter the sleeping wait prior to the load of m_WaitingForTaskCount
    // in this function, so we introduce a gc_TaskAlmostCompleteCount to prevent this.
    ENKI_ASSERT( gc_TaskAlmostCompleteCount == pTask_->m_RunningCount.load( std::memory_order_acquire ) );
    bool bCallWakeThreads = bWakeThreads_ && pTask_->m_WaitingForTaskCount.load( std::memory_order_acquire );

    Dependency* pDependent = pTask_->m_pDependents;

    // Do not access pTask_ below this line unless we have dependencies.
    pTask_->m_RunningCount.store( 0, std::memory_order_release );

    if( bCallWakeThreads )
    {
        WakeThreadsForTaskCompletion();
    }

    while( pDependent )
    {
        // access pTaskToRunOnCompletion member data before incrementing m_DependenciesCompletedCount so
        // they do not get deleted when another thread completes the pTaskToRunOnCompletion
        int32_t dependenciesCount  = pDependent->pTaskToRunOnCompletion->m_DependenciesCount;
        // get temp copy of pDependent so OnDependenciesComplete can delete task if needed.
        Dependency* pDependentCurr = pDependent;
        pDependent                 = pDependent->pNext;
        int32_t prevDeps = pDependentCurr->pTaskToRunOnCompletion->m_DependenciesCompletedCount.fetch_add( 1, std::memory_order_release );
        ENKI_ASSERT( prevDeps < dependenciesCount );
        if( dependenciesCount == ( prevDeps + 1 ) )
        {
            // reset dependencies
            // only safe to access pDependentCurr here after above fetch_add because this is the thread
            // which calls OnDependenciesComplete after store with memory_order_release
            pDependentCurr->pTaskToRunOnCompletion->m_DependenciesCompletedCount.store(
                0,
                std::memory_order_release );
            pDependentCurr->pTaskToRunOnCompletion->OnDependenciesComplete( this, threadNum_ );
        }
    }
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
    // We don't want to suspend this thread if there are task threads
    // with pinned tasks suspended, as it could result in this thread
    // being unsuspended and not the thread with pinned tasks
    if( WakeSuspendedThreadsWithPinnedTasks( threadNum_ ) )
    {
        return;
    }

    // We increment the number of threads waiting here in order
    // to ensure that the check for tasks occurs after the increment
    // to prevent a task being added after a check, then the thread waiting.
    // This will occasionally result in threads being mistakenly awoken,
    // but they will then go back to sleep.
    m_NumThreadsWaitingForNewTasks.fetch_add( 1, std::memory_order_acquire );
    ThreadState prevThreadState = m_pThreadDataStore[threadNum_].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum_].threadState.store( ENKI_THREAD_STATE_WAIT_NEW_TASKS, std::memory_order_seq_cst );

    if( HaveTasks( threadNum_ ) )
    {
        m_NumThreadsWaitingForNewTasks.fetch_sub( 1, std::memory_order_release );
    }
    else
    {
        SafeCallback( m_Config.profilerCallbacks.waitForNewTaskSuspendStart, threadNum_ );
        SemaphoreWait( *m_pNewTaskSemaphore );
        SafeCallback( m_Config.profilerCallbacks.waitForNewTaskSuspendStop, threadNum_ );
    }

    m_pThreadDataStore[threadNum_].threadState.store( prevThreadState, std::memory_order_release );
}

void TaskScheduler::WaitForTaskCompletion( const ICompletable* pCompletable_, uint32_t threadNum_ )
{
    // We don't want to suspend this thread if there are task threads
    // with pinned tasks suspended, as the completable could be a pinned task
    // or it could be waiting on one.
    if( WakeSuspendedThreadsWithPinnedTasks( threadNum_ ) )
    {
        return;
    }

    m_NumThreadsWaitingForTaskCompletion.fetch_add( 1, std::memory_order_acq_rel );
    pCompletable_->m_WaitingForTaskCount.fetch_add( 1, std::memory_order_acq_rel );
    ThreadState prevThreadState = m_pThreadDataStore[threadNum_].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum_].threadState.store( ENKI_THREAD_STATE_WAIT_TASK_COMPLETION, std::memory_order_seq_cst );

    // do not wait on semaphore if task in gc_TaskAlmostCompleteCount state.
    if( gc_TaskAlmostCompleteCount >= pCompletable_->m_RunningCount.load( std::memory_order_acquire ) || HaveTasks( threadNum_ ) )
    {
        m_NumThreadsWaitingForTaskCompletion.fetch_sub( 1, std::memory_order_acq_rel );
    }
    else
    {
        SafeCallback( m_Config.profilerCallbacks.waitForTaskCompleteSuspendStart, threadNum_ );
        std::atomic_thread_fence(std::memory_order_acquire);

        SemaphoreWait( *m_pTaskCompleteSemaphore );
        if( !pCompletable_->GetIsComplete() )
        {
            // This thread which may not the one which was supposed to be awoken
            WakeThreadsForTaskCompletion();
        }
        SafeCallback( m_Config.profilerCallbacks.waitForTaskCompleteSuspendStop, threadNum_ );
    }

    m_pThreadDataStore[threadNum_].threadState.store( prevThreadState, std::memory_order_release );
    pCompletable_->m_WaitingForTaskCount.fetch_sub( 1, std::memory_order_acq_rel );
}

void TaskScheduler::WakeThreadsForNewTasks()
{
    int32_t waiting = m_NumThreadsWaitingForNewTasks.load( std::memory_order_relaxed );
    while( waiting > 0 && !m_NumThreadsWaitingForNewTasks.compare_exchange_weak(waiting, 0, std::memory_order_release, std::memory_order_relaxed ) ) {}

    if( waiting > 0 )
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

bool TaskScheduler::WakeSuspendedThreadsWithPinnedTasks( uint32_t threadNum_ )
{
    for( uint32_t t = 1; t < m_NumThreads; ++t )
    {
        // distribute thread checks more evenly by starting at our thread number rather than 0.
        uint32_t thread = ( threadNum_ + t ) % m_NumThreads;

        ThreadState state = m_pThreadDataStore[ thread ].threadState.load( std::memory_order_acquire );

        ENKI_ASSERT( state != ENKI_THREAD_STATE_NONE );

        if( state == ENKI_THREAD_STATE_WAIT_NEW_TASKS || state == ENKI_THREAD_STATE_WAIT_TASK_COMPLETION )
        {
            // thread is suspended, check if it has pinned tasks
            for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
            {
                if( !m_pPinnedTaskListPerThread[ priority ][ thread ].IsListEmpty() )
                {
                    WakeThreadsForNewTasks();
                    return true;
                }
            }
        }
    }
    return false;
}

void TaskScheduler::SplitAndAddTask( uint32_t threadNum_, SubTaskSet subTask_, uint32_t rangeToSplit_ )
{
    int32_t numAdded = 0;
    int32_t numNewTasksSinceNotification = 0;
    int32_t numRun   = 0;

    int32_t upperBoundNumToAdd = 2 + (int32_t)( ( subTask_.partition.end - subTask_.partition.start ) / rangeToSplit_ );

    // ensure that an artificial completion is not registered whilst adding tasks by incrementing count
    subTask_.pTask->m_RunningCount.fetch_add( upperBoundNumToAdd, std::memory_order_acquire );
    while( subTask_.partition.start != subTask_.partition.end )
    {
        SubTaskSet taskToAdd = SplitTask( subTask_, rangeToSplit_ );

        // add the partition to the pipe
        ++numAdded; ++numNewTasksSinceNotification;
        if( !m_pPipesPerThread[ subTask_.pTask->m_Priority ][ threadNum_ ].WriterTryWriteFront( taskToAdd ) )
        {
            --numAdded; // we were unable to add the task
            if( numNewTasksSinceNotification > 1 )
            {
                WakeThreadsForNewTasks();
            }
            numNewTasksSinceNotification = 0;
            // alter range to run the appropriate fraction
            if( taskToAdd.pTask->m_RangeToRun < taskToAdd.partition.end - taskToAdd.partition.start )
            {
                taskToAdd.partition.end = taskToAdd.partition.start + taskToAdd.pTask->m_RangeToRun;
                ENKI_ASSERT( taskToAdd.partition.end <= taskToAdd.pTask->m_SetSize );
                subTask_.partition.start = taskToAdd.partition.end;
            }
            taskToAdd.pTask->ExecuteRange( taskToAdd.partition, threadNum_ );
            ++numRun;
        }
    }
    int32_t countToRemove = upperBoundNumToAdd - numAdded;
    ENKI_ASSERT( countToRemove > 0 );
    int prevCount = subTask_.pTask->m_RunningCount.fetch_sub( countToRemove, std::memory_order_acq_rel );
    if( countToRemove-1 + gc_TaskStartCount == prevCount )
    {
        TaskComplete( subTask_.pTask, false, threadNum_ );
    }

    // WakeThreadsForNewTasks also calls WakeThreadsForTaskCompletion() so do not need to do so above
    WakeThreadsForNewTasks();
}

TaskSchedulerConfig TaskScheduler::GetConfig() const
{
    return m_Config;
}

void TaskScheduler::AddTaskSetToPipeInt( ITaskSet* pTaskSet_, uint32_t threadNum_ )
{
    ENKI_ASSERT( pTaskSet_->m_RunningCount == gc_TaskStartCount );
    ThreadState prevThreadState = m_pThreadDataStore[threadNum_].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum_].threadState.store( ENKI_THREAD_STATE_RUNNING, std::memory_order_relaxed );
    std::atomic_thread_fence(std::memory_order_acquire);


    // divide task up and add to pipe
    pTaskSet_->m_RangeToRun = pTaskSet_->m_SetSize / m_NumPartitions;
    pTaskSet_->m_RangeToRun = std::max( pTaskSet_->m_RangeToRun, pTaskSet_->m_MinRange );
    // Note: if m_SetSize is < m_RangeToRun this will be handled by SplitTask and so does not need to be handled here

    uint32_t rangeToSplit = pTaskSet_->m_SetSize / m_NumInitialPartitions;
    rangeToSplit = std::max( rangeToSplit, pTaskSet_->m_MinRange );

    SubTaskSet subTask;
    subTask.pTask = pTaskSet_;
    subTask.partition.start = 0;
    subTask.partition.end = pTaskSet_->m_SetSize;
    SplitAndAddTask( threadNum_, subTask, rangeToSplit );
    int prevCount = pTaskSet_->m_RunningCount.fetch_sub(1, std::memory_order_acq_rel );
    if( gc_TaskStartCount == prevCount )
    {
        TaskComplete( pTaskSet_, true, threadNum_ );
    }

    m_pThreadDataStore[threadNum_].threadState.store( prevThreadState, std::memory_order_release );
}

void TaskScheduler::AddTaskSetToPipe( ITaskSet* pTaskSet_ )
{
    ENKI_ASSERT( pTaskSet_->m_RunningCount == 0 );
    InitDependencies( pTaskSet_ );
    pTaskSet_->m_RunningCount.store( gc_TaskStartCount, std::memory_order_relaxed );
    AddTaskSetToPipeInt( pTaskSet_, gtl_threadNum );
}

void  TaskScheduler::AddPinnedTaskInt( IPinnedTask* pTask_ )
{
    ENKI_ASSERT( pTask_->m_RunningCount == gc_TaskStartCount );
    m_pPinnedTaskListPerThread[ pTask_->m_Priority ][ pTask_->threadNum ].WriterWriteFront( pTask_ );

    ThreadState statePinnedTaskThread = m_pThreadDataStore[ pTask_->threadNum ].threadState.load( std::memory_order_acquire );
    if( statePinnedTaskThread == ENKI_THREAD_STATE_WAIT_NEW_PINNED_TASKS )
    {
        SemaphoreSignal( *m_pThreadDataStore[ pTask_->threadNum ].pWaitNewPinnedTaskSemaphore, 1 );
    }
    else
    {
        WakeThreadsForNewTasks();
    }
}

void TaskScheduler::AddPinnedTask( IPinnedTask* pTask_ )
{
    ENKI_ASSERT( pTask_->m_RunningCount == 0 );
    InitDependencies( pTask_ );
    pTask_->m_RunningCount = gc_TaskStartCount;
    AddPinnedTaskInt( pTask_ );
}

void TaskScheduler::InitDependencies( ICompletable* pCompletable_ )
{
    // go through any dependencies and set their running count so they show as not complete
    // and increment dependency count
    if( pCompletable_->m_RunningCount.load( std::memory_order_relaxed ) )
    {
        // already initialized
        return;
    }
    Dependency* pDependent = pCompletable_->m_pDependents;
    while( pDependent )
    {
        InitDependencies( pDependent->pTaskToRunOnCompletion );
        pDependent->pTaskToRunOnCompletion->m_RunningCount.store( gc_TaskStartCount, std::memory_order_relaxed );
        pDependent = pDependent->pNext;
    }
}


void TaskScheduler::RunPinnedTasks()
{
    ENKI_ASSERT( gtl_threadNum != enki::NO_THREAD_NUM );
    uint32_t threadNum = gtl_threadNum;
    ThreadState prevThreadState = m_pThreadDataStore[threadNum].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum].threadState.store( ENKI_THREAD_STATE_RUNNING, std::memory_order_relaxed );
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
            pPinnedTaskSet->m_RunningCount.fetch_sub(1,std::memory_order_acq_rel);
            TaskComplete( pPinnedTaskSet, true, threadNum_ );
        }
    } while( pPinnedTaskSet );
}

void    TaskScheduler::WaitforTask( const ICompletable* pCompletable_, enki::TaskPriority priorityOfLowestToRun_ )
{
    ENKI_ASSERT( gtl_threadNum != enki::NO_THREAD_NUM );
    uint32_t threadNum = gtl_threadNum;
    uint32_t hintPipeToCheck_io = threadNum + 1;    // does not need to be clamped.

    // waiting for a task is equivalent to 'running' for thread state purpose as we may run tasks whilst waiting
    ThreadState prevThreadState = m_pThreadDataStore[threadNum].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum].threadState.store( ENKI_THREAD_STATE_RUNNING, std::memory_order_relaxed );
    std::atomic_thread_fence(std::memory_order_acquire);


    if( pCompletable_ && !pCompletable_->GetIsComplete() )
    {
        SafeCallback( m_Config.profilerCallbacks.waitForTaskCompleteStart, threadNum );
        // We need to ensure that the task we're waiting on can complete even if we're the only thread,
        // so we clamp the priorityOfLowestToRun_ to no smaller than the task we're waiting for
        priorityOfLowestToRun_ = std::max( priorityOfLowestToRun_, pCompletable_->m_Priority );
        uint32_t spinCount = 0;
        while( !pCompletable_->GetIsComplete() && GetIsRunning() )
        {
            ++spinCount;
            for( int priority = 0; priority <= priorityOfLowestToRun_; ++priority )
            {
                if( TryRunTask( threadNum, priority, hintPipeToCheck_io ) )
                {
                    spinCount = 0; // reset spin as ran a task
                    break;
                }
            }
            if( spinCount > gc_SpinCount )
            {
                WaitForTaskCompletion( pCompletable_, threadNum );
                spinCount = 0;
            }
            else
            {
                uint32_t spinBackoffCount = spinCount * gc_SpinBackOffMultiplier;
                SpinWait( spinBackoffCount );
            }
        }
        SafeCallback( m_Config.profilerCallbacks.waitForTaskCompleteStop, threadNum );
    }
    else if( nullptr == pCompletable_ )
    {
            for( int priority = 0; priority <= priorityOfLowestToRun_; ++priority )
            {
                if( TryRunTask( threadNum, priority, hintPipeToCheck_io ) )
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
    ENKI_ASSERT( gtl_threadNum != enki::NO_THREAD_NUM );
    m_bWaitforAllCalled.store( true, std::memory_order_release );

    bool bHaveTasks = true;
    uint32_t ourThreadNum = gtl_threadNum;
    uint32_t hintPipeToCheck_io = ourThreadNum  + 1;    // does not need to be clamped.
    bool otherThreadsRunning = false; // account for this thread
    uint32_t spinCount = 0;
    TaskSchedulerWaitTask dummyWaitTask;
    dummyWaitTask.threadNum = 0;
    while( GetIsRunning() && ( bHaveTasks || otherThreadsRunning ) )
    {
        bHaveTasks = TryRunTask( ourThreadNum, hintPipeToCheck_io );
        ++spinCount;
        if( bHaveTasks )
        {
            spinCount = 0; // reset spin as ran a task
        }
        if( spinCount > gc_SpinCount )
        {
            // find a running thread and add a dummy wait task
            int32_t countThreadsToCheck = m_NumThreads - 1;
            bool bHaveThreadToWaitOn = false;
            do
            {
                --countThreadsToCheck;
                dummyWaitTask.threadNum = ( dummyWaitTask.threadNum + 1 ) % m_NumThreads;

                // We can only add a pinned task to wait on if we find an enki Task Thread which isn't this thread.
                // Otherwise, we have to busy wait.
                if( dummyWaitTask.threadNum != ourThreadNum && dummyWaitTask.threadNum > m_Config.numExternalTaskThreads )
                {
                    ThreadState state = m_pThreadDataStore[ dummyWaitTask.threadNum ].threadState.load( std::memory_order_acquire );
                    if( state == ENKI_THREAD_STATE_RUNNING || state == ENKI_THREAD_STATE_WAIT_TASK_COMPLETION )
                    {
                        bHaveThreadToWaitOn = true;
                        break;
                    }
                }
            } while( countThreadsToCheck );

            if( bHaveThreadToWaitOn )
            {
                ENKI_ASSERT( dummyWaitTask.threadNum != ourThreadNum );
                AddPinnedTask( &dummyWaitTask );
                WaitforTask( &dummyWaitTask );
            }
            spinCount = 0;
        }
        else
        {
            uint32_t spinBackoffCount = spinCount * gc_SpinBackOffMultiplier;
            SpinWait( spinBackoffCount );
        }

        // count threads running
        otherThreadsRunning = false;
        for(uint32_t thread = 0; thread < m_NumThreads && !otherThreadsRunning; ++thread )
        {
            // ignore our thread
            if( thread != ourThreadNum )
            {
                switch( m_pThreadDataStore[thread].threadState.load( std::memory_order_acquire ) )
                {
                case ENKI_THREAD_STATE_NONE:
                    ENKI_ASSERT(false);
                    break;
                case ENKI_THREAD_STATE_NOT_LAUNCHED:
                case ENKI_THREAD_STATE_RUNNING:
                case ENKI_THREAD_STATE_WAIT_TASK_COMPLETION:
                    otherThreadsRunning = true;
                    break;
                case ENKI_THREAD_STATE_WAIT_NEW_PINNED_TASKS:
                    otherThreadsRunning = true;
                    SemaphoreSignal( *m_pThreadDataStore[thread].pWaitNewPinnedTaskSemaphore, 1 );
                    break;
                case ENKI_THREAD_STATE_PRIMARY_REGISTERED:
                case ENKI_THREAD_STATE_EXTERNAL_REGISTERED:
                case ENKI_THREAD_STATE_EXTERNAL_UNREGISTERED:
                case ENKI_THREAD_STATE_WAIT_NEW_TASKS:
                case ENKI_THREAD_STATE_STOPPED:
                    break;
                }
            }
        }
        if( !otherThreadsRunning )
        {
            // check there are no tasks
            for(uint32_t thread = 0; thread < m_NumThreads && !otherThreadsRunning; ++thread )
            {
                // ignore our thread
                if( thread != ourThreadNum )
                {
                    otherThreadsRunning = HaveTasks( thread );
                }
            }
        }
     }

    m_bWaitforAllCalled.store( false, std::memory_order_release );
}

void TaskScheduler::WaitforAllAndShutdown()
{
    m_bWaitforAllCalled.store( true, std::memory_order_release );
    m_bShutdownRequested.store( true, std::memory_order_release );
    if( m_bHaveThreads )
    {
        WaitforAll();
        StopThreads(true);
    }
}

void TaskScheduler::ShutdownNow()
{
    m_bWaitforAllCalled.store( true, std::memory_order_release );
    m_bShutdownRequested.store( true, std::memory_order_release );
    if( m_bHaveThreads )
    {
        StopThreads(true);
    }
}

void TaskScheduler::WaitForNewPinnedTasks()
{
    ENKI_ASSERT( gtl_threadNum != enki::NO_THREAD_NUM );
    uint32_t threadNum = gtl_threadNum;
    ThreadState prevThreadState = m_pThreadDataStore[threadNum].threadState.load( std::memory_order_relaxed );
    m_pThreadDataStore[threadNum].threadState.store( ENKI_THREAD_STATE_WAIT_NEW_PINNED_TASKS, std::memory_order_seq_cst );

    // check if have tasks inside threadState change but before waiting
    bool bHavePinnedTasks = false;
    for( int priority = 0; priority < TASK_PRIORITY_NUM; ++priority )
    {
        if( !m_pPinnedTaskListPerThread[ priority ][ threadNum ].IsListEmpty() )
        {
            bHavePinnedTasks = true;
            break;
        }
    }

    if( !bHavePinnedTasks )
    {
        SafeCallback( m_Config.profilerCallbacks.waitForNewTaskSuspendStart, threadNum );
        SemaphoreWait( *m_pThreadDataStore[threadNum].pWaitNewPinnedTaskSemaphore );
        SafeCallback( m_Config.profilerCallbacks.waitForNewTaskSuspendStop, threadNum );
    }

    m_pThreadDataStore[threadNum].threadState.store( prevThreadState, std::memory_order_release );
}


uint32_t        TaskScheduler::GetNumTaskThreads() const
{
    return m_NumThreads;
}


uint32_t TaskScheduler::GetThreadNum() const
{
    return gtl_threadNum;
}

template<typename T>
T* TaskScheduler::NewArray( size_t num_, const char* file_, int line_  )
{
    T* pRet = (T*)m_Config.customAllocator.alloc( alignof(T), num_*sizeof(T), m_Config.customAllocator.userData, file_, line_ );
    if( !std::is_trivial<T>::value )
    {
        T* pCurr = pRet;
        for( size_t i = 0; i < num_; ++i )
        {
            void* pBuffer = pCurr;
            pCurr = new(pBuffer) T;
            ++pCurr;
        }
    }
    return pRet;
}

template<typename T>
void TaskScheduler::DeleteArray( T* p_, size_t num_, const char* file_, int line_ )
{
    if( !std::is_trivially_destructible<T>::value )
    {
        size_t i = num_;
        while(i)
        {
            p_[--i].~T();
        }
    }
    m_Config.customAllocator.free( p_, sizeof(T)*num_, m_Config.customAllocator.userData, file_, line_ );
}

template<class T, class... Args>
T* TaskScheduler::New( const char* file_, int line_, Args&&... args_ )
{
    T* pRet = this->Alloc<T>( file_, line_ );
    return new(pRet) T( std::forward<Args>(args_)... );
}

template< typename T >
void TaskScheduler::Delete( T* p_, const char* file_, int line_  )
{
    p_->~T();
    this->Free(p_, file_, line_ );
}

template< typename T >
T* TaskScheduler::Alloc( const char* file_, int line_  )
{
    T* pRet = (T*)m_Config.customAllocator.alloc( alignof(T), sizeof(T), m_Config.customAllocator.userData, file_, line_ );
    return pRet;
}

template< typename T >
void TaskScheduler::Free( T* p_, const char* file_, int line_  )
{
    m_Config.customAllocator.free( p_, sizeof(T), m_Config.customAllocator.userData, file_, line_ );
}

TaskScheduler::TaskScheduler()
        : m_pPipesPerThread()
        , m_pPinnedTaskListPerThread()
        , m_NumThreads(0)
        , m_pThreadDataStore(NULL)
        , m_pThreads(NULL)
        , m_bRunning(false)
        , m_NumInternalTaskThreadsRunning(0)
        , m_NumThreadsWaitingForNewTasks(0)
        , m_NumThreadsWaitingForTaskCompletion(0)
        , m_NumPartitions(0)
        , m_pNewTaskSemaphore(NULL)
        , m_pTaskCompleteSemaphore(NULL)
        , m_NumInitialPartitions(0)
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
    ENKI_ASSERT( numThreadsTotal_ >= 1 );
    StopThreads( true ); // Stops threads, waiting for them.
    m_Config.numTaskThreadsToCreate = numThreadsTotal_ - 1;
    m_Config.numExternalTaskThreads = 0;
    StartThreads();}

void TaskScheduler::Initialize( TaskSchedulerConfig config_ )
{
    StopThreads( true ); // Stops threads, waiting for them.
    m_Config = config_;
    StartThreads();
}

void TaskScheduler::Initialize()
{
    Initialize( std::thread::hardware_concurrency() );
}

// Semaphore implementation
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace enki
{
    struct semaphoreid_t
    {
        HANDLE      sem;
    };

    inline void SemaphoreCreate( semaphoreid_t& semaphoreid )
    {
#ifdef _XBOX_ONE
        semaphoreid.sem = CreateSemaphoreExW( NULL, 0, MAXLONG, NULL, 0, SEMAPHORE_ALL_ACCESS );
#else
        semaphoreid.sem = CreateSemaphore( NULL, 0, MAXLONG, NULL );
#endif
    }

    inline void SemaphoreClose( semaphoreid_t& semaphoreid )
    {
        CloseHandle( semaphoreid.sem );
    }

    inline void SemaphoreWait( semaphoreid_t& semaphoreid  )
    {
        DWORD retval = WaitForSingleObject( semaphoreid.sem, INFINITE );
        ENKI_ASSERT( retval != WAIT_FAILED );
        (void)retval; // only needed for ENKI_ASSERT
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
// Mach semaphores can now only be created by the kernel
// Named semaphores work, but would require unique name construction to ensure
// they are isolated to this process.
// Dispatch semaphores appear to be the way other developers use OSX Semaphores, e.g. Boost
// However the API could change
// OSX below 10.6 does not support dispatch, but I do not have an earlier OSX version
// to test alternatives
#include <dispatch/dispatch.h>

namespace enki
{

    struct semaphoreid_t
    {
        dispatch_semaphore_t   sem;
    };

    inline void SemaphoreCreate( semaphoreid_t& semaphoreid )
    {
        semaphoreid.sem = dispatch_semaphore_create(0);
    }

    inline void SemaphoreClose( semaphoreid_t& semaphoreid )
    {
        dispatch_release( semaphoreid.sem );
    }

    inline void SemaphoreWait( semaphoreid_t& semaphoreid  )
    {
        dispatch_semaphore_wait( semaphoreid.sem, DISPATCH_TIME_FOREVER );
    }

    inline void SemaphoreSignal( semaphoreid_t& semaphoreid, int32_t countWaiting )
    {
        while( countWaiting-- > 0 )
        {
            dispatch_semaphore_signal( semaphoreid.sem );
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
        ENKI_ASSERT( err == 0 );
        (void)err;
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

semaphoreid_t* TaskScheduler::SemaphoreNew()
{
    semaphoreid_t* pSemaphore = this->Alloc<semaphoreid_t>( ENKI_FILE_AND_LINE );
    SemaphoreCreate( *pSemaphore );
    return pSemaphore;
}

void TaskScheduler::SemaphoreDelete( semaphoreid_t* pSemaphore_ )
{
    SemaphoreClose( *pSemaphore_ );
    this->Free( pSemaphore_, ENKI_FILE_AND_LINE );
}

void TaskScheduler::SetCustomAllocator( CustomAllocator customAllocator_ )
{
    m_Config.customAllocator = customAllocator_;
}

Dependency::Dependency( const ICompletable* pDependencyTask_, ICompletable* pTaskToRunOnCompletion_ )
    : pTaskToRunOnCompletion( pTaskToRunOnCompletion_ )
    , pDependencyTask( pDependencyTask_ )
    , pNext( pDependencyTask->m_pDependents )
{
    ENKI_ASSERT( pDependencyTask->GetIsComplete() );
    ENKI_ASSERT( pTaskToRunOnCompletion->GetIsComplete() );
    pDependencyTask->m_pDependents = this;
    ++pTaskToRunOnCompletion->m_DependenciesCount;
}

Dependency::Dependency( Dependency&& rhs_ ) noexcept
{
    pDependencyTask   = rhs_.pDependencyTask;
    pTaskToRunOnCompletion = rhs_.pTaskToRunOnCompletion;
    pNext             = rhs_.pNext;
    if( rhs_.pDependencyTask )
    {
        ENKI_ASSERT( rhs_.pTaskToRunOnCompletion );
        ENKI_ASSERT( rhs_.pDependencyTask->GetIsComplete() );
        ENKI_ASSERT( rhs_.pTaskToRunOnCompletion->GetIsComplete() );
        Dependency** ppDependent = &(pDependencyTask->m_pDependents);
        while( *ppDependent )
        {
            if( &rhs_ == *ppDependent )
            {
                *ppDependent = this;
                break;
            }
            ppDependent = &((*ppDependent)->pNext);
        }
    }
}


Dependency::~Dependency()
{
    ClearDependency();
}

void Dependency::SetDependency( const ICompletable* pDependencyTask_, ICompletable* pTaskToRunOnCompletion_ )
{
    ClearDependency();
    ENKI_ASSERT( pDependencyTask_->GetIsComplete() );
    ENKI_ASSERT( pTaskToRunOnCompletion_->GetIsComplete() );
    pDependencyTask = pDependencyTask_;
    pTaskToRunOnCompletion = pTaskToRunOnCompletion_;
    pNext = pDependencyTask->m_pDependents;
    pDependencyTask->m_pDependents = this;
    ++pTaskToRunOnCompletion->m_DependenciesCount;
}

void Dependency::ClearDependency()
{
    if( pDependencyTask )
    {
        ENKI_ASSERT( pTaskToRunOnCompletion );
        ENKI_ASSERT( pDependencyTask->GetIsComplete() );
        ENKI_ASSERT( pTaskToRunOnCompletion->GetIsComplete() );
        ENKI_ASSERT( pTaskToRunOnCompletion->m_DependenciesCount > 0 );
        Dependency* pDependent = pDependencyTask->m_pDependents;
        --pTaskToRunOnCompletion->m_DependenciesCount;
        if( this == pDependent )
        {
            pDependencyTask->m_pDependents = pDependent->pNext;
        }
        else
        {
            while( pDependent )
            {
                Dependency* pPrev = pDependent;
                pDependent = pDependent->pNext;
                if( this == pDependent )
                {
                    pPrev->pNext = pDependent->pNext;
                    break;
                }
            }
        }
    }
    pDependencyTask = NULL;
    pDependencyTask =  NULL;
    pNext = NULL;
}
