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

#pragma once

#if   defined(_WIN32) && defined(ENKITS_BUILD_DLL)
    // Building enkiTS as a DLL
    #define ENKITS_API __declspec(dllexport)
#elif defined(_WIN32) && defined(ENKITS_DLL)
    // Using enkiTS as a DLL
    #define ENKITS_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(ENKITS_BUILD_DLL)
    // Building enkiTS as a shared library
    #define ENKITS_API __attribute__((visibility("default")))
#else
    #define ENKITS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct enkiTaskScheduler enkiTaskScheduler;
typedef struct enkiTaskSet       enkiTaskSet;
typedef struct enkiPinnedTask    enkiPinnedTask;

typedef void (* enkiTaskExecuteRange)( uint32_t start_, uint32_t end, uint32_t threadnum_, void* pArgs_ );
typedef void (* enkiPinnedTaskExecute)( void* pArgs_ );

// TaskScheduler implements several callbacks intended for profilers
typedef void (*enkiProfilerCallbackFunc)( uint32_t threadnum_ );
struct enkiProfilerCallbacks
{
    enkiProfilerCallbackFunc threadStart;
    enkiProfilerCallbackFunc threadStop;
    enkiProfilerCallbackFunc waitForNewTaskSuspendStart;      // thread suspended waiting for new tasks
    enkiProfilerCallbackFunc waitForNewTaskSuspendStop;       // thread unsuspended
    enkiProfilerCallbackFunc waitForTaskCompleteStart;        // thread waiting for task completion
    enkiProfilerCallbackFunc waitForTaskCompleteStop;         // thread stopped waiting
    enkiProfilerCallbackFunc waitForTaskCompleteSuspendStart; // thread suspended waiting task completion
    enkiProfilerCallbackFunc waitForTaskCompleteSuspendStop;  // thread unsuspended
};

struct enkiTaskSchedulerConfig
{
    // numTaskThreadsToCreate - Number of tasking threads the task scheduler will create. Must be > 0.
    // Defaults to GetNumHardwareThreads()-1 threads as thread which calls initialize is thread 0.
    uint32_t              numTaskThreadsToCreate;

    // numExternalTaskThreads - Advanced use. Number of external threads which need to use TaskScheduler API.
    // See enkiRegisterExternalTaskThread() for usage.
    // Defaults to 0, the thread used to initialize the TaskScheduler. 
    uint32_t              numExternalTaskThreads;

    struct enkiProfilerCallbacks profilerCallbacks;
};


// Create a new task scheduler
ENKITS_API enkiTaskScheduler*  enkiNewTaskScheduler();

// Get config. Can be called before enkiInitTaskSchedulerWithConfig to get the defaults
ENKITS_API struct enkiTaskSchedulerConfig enkiGetTaskSchedulerConfig( enkiTaskScheduler* pETS_ );

// Initialize task scheduler - will create GetNumHardwareThreads()-1 threads, which is
// sufficient to fill the system when including the main thread.
// Initialize can be called multiple times - it will wait for completion
// before re-initializing.
ENKITS_API void                enkiInitTaskScheduler(  enkiTaskScheduler* pETS_ );

// Initialize a task scheduler with numThreads_ (must be > 0)
// will create numThreads_-1 threads, as thread 0 is
// the thread on which the initialize was called.
ENKITS_API void                enkiInitTaskSchedulerNumThreads( enkiTaskScheduler* pETS_, uint32_t numThreads_ );

// Initialize a task scheduler with config, see enkiTaskSchedulerConfig for details
ENKITS_API void                enkiInitTaskSchedulerWithConfig( enkiTaskScheduler* pETS_, struct enkiTaskSchedulerConfig config_ );


// Delete a task scheduler
ENKITS_API void                enkiDeleteTaskScheduler( enkiTaskScheduler* pETS_ );

// Create a task set.
ENKITS_API enkiTaskSet*        enkiCreateTaskSet( enkiTaskScheduler* pETS_, enkiTaskExecuteRange taskFunc_  );

// Delete a task set.
ENKITS_API void                enkiDeleteTaskSet( enkiTaskSet* pTaskSet_ );

// Set task priority ( 0 to ENKITS_TASK_PRIORITIES_NUM-1, where 0 is highest)
ENKITS_API void                enkiSetPriorityTaskSet( enkiTaskSet* pTaskSet_, int priority_ );

// Schedule the task
ENKITS_API void                enkiAddTaskSetToPipe( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_,
                                           void* pArgs_, uint32_t setSize_ );

// Schedule the task with a minimum range.
// This should be set to a value which results in computation effort of at least 10k
// clock cycles to minimize tast scheduler overhead.
// NOTE: The last partition will be smaller than m_MinRange if m_SetSize is not a multiple
// of m_MinRange.
// Also known as grain size in literature.
ENKITS_API void                enkiAddTaskSetToPipeMinRange( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_,
                                                  void* pArgs_, uint32_t setSize_, uint32_t minRange_ );


// Check if TaskSet is complete. Doesn't wait. Returns 1 if complete, 0 if not.
ENKITS_API int                 enkiIsTaskSetComplete( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ );

// Create a pinned task.
ENKITS_API enkiPinnedTask*     enkiCreatePinnedTask( enkiTaskScheduler* pETS_, enkiPinnedTaskExecute taskFunc_, uint32_t threadNum_  );

// Delete a pinned task.
ENKITS_API void                enkiDeletePinnedTask( enkiPinnedTask* pTask_ );

// Set PinnedTask ( 0 to ENKITS_TASK_PRIORITIES_NUM-1, where 0 is highest)
ENKITS_API void                enkiSetPriorityPinnedTask( enkiPinnedTask* pTask_, int priority_ );

// Schedule a pinned task
// Pinned tasks can be added from any thread
ENKITS_API void                enkiAddPinnedTask( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_,
                                           void* pArgs_ );

// This function will run any enkiPinnedTask* for current thread, but not run other
// Main thread should call this or use a wait to ensure it's tasks are run.
ENKITS_API void                enkiRunPinnedTasks( enkiTaskScheduler * pETS_ );

// Check if enkiPinnedTask is complete. Doesn't wait. Returns 1 if complete, 0 if not.
ENKITS_API int                 enkiIsPinnedTaskComplete( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_ );

// Wait for a given task.
// should only be called from thread which created the taskscheduler , or within a task
// if called with 0 it will try to run tasks, and return if none available.
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForTaskSet( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ );

// enkiWaitForTaskSetPriority as enkiWaitForTaskSet but only runs other tasks with priority <= maxPriority_
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForTaskSetPriority( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_, int maxPriority_ );

// Wait for a given pinned task.
// should only be called from thread which created the taskscheduler, or within a task
// if called with 0 it will try to run tasks, and return if none available.
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForPinnedTask( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_ );

// enkiWaitForPinnedTaskPriority as enkiWaitForPinnedTask but only runs other tasks with priority <= maxPriority_
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForPinnedTaskPriority( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_, int maxPriority_ );

// Waits for all task sets to complete - not guaranteed to work unless we know we
// are in a situation where tasks aren't being continuosly added.
ENKITS_API void                enkiWaitForAll( enkiTaskScheduler* pETS_ );

// Returns the number of threads created for running tasks + number of external threads
// plus 1 to account for the thread used to initialize the task scheduler.
// Equivalent to config values: numTaskThreadsToCreate + numExternalTaskThreads + 1.
// It is guaranteed that enkiGetThreadNum() < enkiGetNumTaskThreads()
ENKITS_API uint32_t            enkiGetNumTaskThreads( enkiTaskScheduler* pETS_ );

// Returns the current task threadNum
// Will return 0 for thread which initialized the task scheduler,
// and all other non-enkiTS threads which have not been registered ( see enkiRegisterExternalTaskThread() ),
// and < enkiGetNumTaskThreads() for all threads.
// It is guaranteed that enkiGetThreadNum() < enkiGetNumTaskThreads()
ENKITS_API uint32_t            enkiGetThreadNum( enkiTaskScheduler* pETS_ );

// Call on a thread to register the thread to use the TaskScheduling API.
// This is implicitly done for the thread which initializes the TaskScheduler
// Intended for developers who have threads who need to call the TaskScheduler API
// Returns true if successfull, false if not.
// Can only have numExternalTaskThreads registered at any one time, which must be set
// at initialization time.
ENKITS_API int                 enkiRegisterExternalTaskThread( enkiTaskScheduler* pETS_);

// Call on a thread on which RegisterExternalTaskThread has been called to deregister that thread.
ENKITS_API void                enkiDeRegisterExternalTaskThread( enkiTaskScheduler* pETS_);

// Get the number of registered external task threads.
ENKITS_API uint32_t            enkiGetNumRegisteredExternalTaskThreads( enkiTaskScheduler* pETS_);

// ------------- Start DEPRECATED Functions -------------
// DEPRECATED - enkiGetProfilerCallbacks.  Use enkiTaskSchedulerConfig instead
// Get the callback structure so it can be set 
ENKITS_API struct enkiProfilerCallbacks*    enkiGetProfilerCallbacks( enkiTaskScheduler* pETS_ );
// -------------  End DEPRECATED Functions  -------------


#ifdef __cplusplus
}
#endif
