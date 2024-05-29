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

// Define ENKI_CUSTOM_ALLOC_FILE_AND_LINE (at project level) to get file and line report in custom allocators,
// this is default in Debug - to turn off define ENKI_CUSTOM_ALLOC_NO_FILE_AND_LINE
#ifndef ENKI_CUSTOM_ALLOC_FILE_AND_LINE
#if defined(_DEBUG ) && !defined(ENKI_CUSTOM_ALLOC_NO_FILE_AND_LINE)
#define ENKI_CUSTOM_ALLOC_FILE_AND_LINE
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef struct enkiTaskScheduler    enkiTaskScheduler;
typedef struct enkiTaskSet          enkiTaskSet;
typedef struct enkiPinnedTask       enkiPinnedTask;
typedef struct enkiCompletable      enkiCompletable;
typedef struct enkiDependency       enkiDependency;
typedef struct enkiCompletionAction enkiCompletionAction;

static const uint32_t ENKI_NO_THREAD_NUM = 0xFFFFFFFF;

typedef void (* enkiTaskExecuteRange)( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ );
typedef void (* enkiPinnedTaskExecute)( void* pArgs_ );
typedef void (* enkiCompletionFunction)( void* pArgs_, uint32_t threadNum_ );

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

// Custom allocator, set in enkiTaskSchedulerConfig. Also see ENKI_CUSTOM_ALLOC_FILE_AND_LINE for file_ and line_
typedef void* (*enkiAllocFunc)( size_t align_, size_t size_, void* userData_, const char* file_, int line_ );
typedef void  (*enkiFreeFunc)(  void* ptr_,    size_t size_, void* userData_, const char* file_, int line_ );
ENKITS_API void* enkiDefaultAllocFunc(  size_t align_, size_t size_, void* userData_, const char* file_, int line_ );
ENKITS_API void  enkiDefaultFreeFunc(   void* ptr_,    size_t size_, void* userData_, const char* file_, int line_ );
struct enkiCustomAllocator
{
    enkiAllocFunc alloc;
    enkiFreeFunc  free;
    void*         userData;
};

struct enkiParamsTaskSet
{
    void*    pArgs;
    uint32_t setSize;
    uint32_t minRange;
    int      priority;
};

struct enkiParamsPinnedTask
{
    void*    pArgs;
    int      priority;
};

struct enkiParamsCompletionAction
{
    void*                  pArgsPreComplete;
    void*                  pArgsPostComplete;
    const enkiCompletable* pDependency; // task which when complete triggers completion function
};

// enkiTaskSchedulerConfig - configuration struct for advanced Initialize
// Always use enkiGetTaskSchedulerConfig() to get defaults prior to altering and
// initializing with enkiInitTaskSchedulerWithConfig().
struct enkiTaskSchedulerConfig
{
    // numTaskThreadsToCreate - Number of tasking threads the task scheduler will create. Must be > 0.
    // Defaults to GetNumHardwareThreads()-1 threads as thread which calls initialize is thread 0.
    uint32_t              numTaskThreadsToCreate;

    // numExternalTaskThreads - Advanced use. Number of external threads which need to use TaskScheduler API.
    // See TaskScheduler::RegisterExternalTaskThread() for usage.
    // Defaults to 0. The thread used to initialize the TaskScheduler can also use the TaskScheduler API.
    // Thus there are (numTaskThreadsToCreate + numExternalTaskThreads + 1) able to use the API, with this
    // defaulting to the number of hardware threads available to the system.
    uint32_t              numExternalTaskThreads;

    struct enkiProfilerCallbacks profilerCallbacks;

    struct enkiCustomAllocator   customAllocator;
};


/* ----------------------------  Task Scheduler  ---------------------------- */
// Create a new task scheduler
ENKITS_API enkiTaskScheduler*  enkiNewTaskScheduler( void );

// Create a new task scheduler using a custom allocator
// This will  use the custom allocator to allocate the task scheduler struct
// and additionally will set the custom allocator in enkiTaskSchedulerConfig of the task scheduler
ENKITS_API enkiTaskScheduler*  enkiNewTaskSchedulerWithCustomAllocator( struct enkiCustomAllocator customAllocator_ );

// Get config. Can be called before enkiInitTaskSchedulerWithConfig to get the defaults
ENKITS_API struct enkiTaskSchedulerConfig enkiGetTaskSchedulerConfig( enkiTaskScheduler* pETS_ );

// DEPRECATED: use GetIsShutdownRequested() instead of GetIsRunning() in external code
// while( enkiGetIsRunning(pETS) ) {} can be used in tasks which loop, to check if enkiTS has been shutdown.
// If enkiGetIsRunning() returns false should then exit. Not required for finite tasks
ENKITS_API int                 enkiGetIsRunning( enkiTaskScheduler* pETS_ );

// while( !enkiGetIsShutdownRequested() ) {} can be used in tasks which loop, to check if enkiTS has been requested to shutdown.
// If enkiGetIsShutdownRequested() returns true should then exit. Not required for finite tasks
// Safe to use with enkiWaitforAllAndShutdown() where this will be set
// Not safe to use with enkiWaitforAll(), use enkiGetIsWaitforAllCalled() instead.
ENKITS_API int                 enkiGetIsShutdownRequested( enkiTaskScheduler* pETS_ );

// while( !enkiGetIsWaitforAllCalled() ) {} can be used in tasks which loop, to check if enkiWaitforAll() has been called.
// If enkiGetIsWaitforAllCalled() returns false should then exit. Not required for finite tasks
// This is intended to be used with code which calls enkiWaitforAll().
// This is also set when the task manager is shutting down, so no need to have an additional check for enkiGetIsShutdownRequested()
ENKITS_API int                 enkiGetIsWaitforAllCalled( enkiTaskScheduler* pETS_ );

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

// Waits for all task sets to complete and shutdown threads - not guaranteed to work unless we know we
// are in a situation where tasks aren't being continuously added.
// pETS_ can then be reused.
// This function can be safely called even if enkiInit* has not been called.
ENKITS_API void                enkiWaitforAllAndShutdown( enkiTaskScheduler* pETS_ );

// Delete a task scheduler.
ENKITS_API void                enkiDeleteTaskScheduler( enkiTaskScheduler* pETS_ );

// Waits for all task sets to complete - not guaranteed to work unless we know we
// are in a situation where tasks aren't being continuously added.
ENKITS_API void                enkiWaitForAll( enkiTaskScheduler* pETS_ );

// Returns the number of threads created for running tasks + number of external threads
// plus 1 to account for the thread used to initialize the task scheduler.
// Equivalent to config values: numTaskThreadsToCreate + numExternalTaskThreads + 1.
// It is guaranteed that enkiGetThreadNum() < enkiGetNumTaskThreads()
ENKITS_API uint32_t            enkiGetNumTaskThreads( enkiTaskScheduler* pETS_ );

// Returns the current task threadNum.
// Will return 0 for thread which initialized the task scheduler,
// and ENKI_NO_THREAD_NUM for all other non-enkiTS threads which have not been registered ( see enkiRegisterExternalTaskThread() ),
// and < enkiGetNumTaskThreads() for all registered and internal enkiTS threads.
// It is guaranteed that enkiGetThreadNum() < enkiGetNumTaskThreads() unless it is ENKI_NO_THREAD_NUM
ENKITS_API uint32_t            enkiGetThreadNum( enkiTaskScheduler* pETS_ );

// Call on a thread to register the thread to use the TaskScheduling API.
// This is implicitly done for the thread which initializes the TaskScheduler
// Intended for developers who have threads who need to call the TaskScheduler API
// Returns true if successful, false if not.
// Can only have numExternalTaskThreads registered at any one time, which must be set
// at initialization time.
ENKITS_API int                 enkiRegisterExternalTaskThread( enkiTaskScheduler* pETS_ );

// As enkiRegisterExternalTaskThread() but explicitly requests a given thread number.
// threadNumToRegister_ must be  >= GetNumFirstExternalTaskThread()
// and < ( GetNumFirstExternalTaskThread() + numExternalTaskThreads )
ENKITS_API int                 enkiRegisterExternalTaskThreadNum( enkiTaskScheduler* pETS_, uint32_t threadNumToRegister_ );

// Call on a thread on which RegisterExternalTaskThread has been called to deregister that thread.
ENKITS_API void                enkiDeRegisterExternalTaskThread( enkiTaskScheduler* pETS_);

// Get the number of registered external task threads.
ENKITS_API uint32_t            enkiGetNumRegisteredExternalTaskThreads( enkiTaskScheduler* pETS_ );

// Get the thread number of the first external task thread. This thread
// is not guaranteed to be registered, but threads are registered in order
// from GetNumFirstExternalTaskThread() up to ( GetNumFirstExternalTaskThread() + numExternalTaskThreads )
// Note that if numExternalTaskThreads == 0 a for loop using this will be valid:
// for( uint32_t externalThreadNum = GetNumFirstExternalTaskThread();
//      externalThreadNum < ( GetNumFirstExternalTaskThread() + numExternalTaskThreads
//      ++externalThreadNum ) { // do something with externalThreadNum }
ENKITS_API uint32_t            enkiGetNumFirstExternalTaskThread( void );


/* ----------------------------     TaskSets    ---------------------------- */
// Create a task set.
ENKITS_API enkiTaskSet*        enkiCreateTaskSet( enkiTaskScheduler* pETS_, enkiTaskExecuteRange taskFunc_  );

// Delete a task set.
ENKITS_API void                enkiDeleteTaskSet( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ );

// Get task parameters via enkiParamsTaskSet
ENKITS_API struct enkiParamsTaskSet enkiGetParamsTaskSet( enkiTaskSet* pTaskSet_ );

// Set task parameters via enkiParamsTaskSet
ENKITS_API void                enkiSetParamsTaskSet( enkiTaskSet* pTaskSet_, struct enkiParamsTaskSet params_ );

// Set task priority ( 0 to ENKITS_TASK_PRIORITIES_NUM-1, where 0 is highest)
ENKITS_API void                enkiSetPriorityTaskSet( enkiTaskSet* pTaskSet_, int priority_ );

// Set TaskSet args
ENKITS_API void                enkiSetArgsTaskSet( enkiTaskSet* pTaskSet_, void* pArgs_ );

// Set TaskSet set setSize
ENKITS_API void                enkiSetSetSizeTaskSet( enkiTaskSet* pTaskSet_, uint32_t setSize_ );

// Set TaskSet set min range
ENKITS_API void                enkiSetMinRangeTaskSet( enkiTaskSet* pTaskSet_, uint32_t minRange_ );

// Schedule the task, use parameters set with enkiSet*TaskSet
ENKITS_API void                enkiAddTaskSet( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ );

// Schedule the task
// overwrites args previously set with enkiSetArgsTaskSet
// overwrites setSize previously set with enkiSetSetSizeTaskSet
ENKITS_API void                enkiAddTaskSetArgs( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_,
                                           void* pArgs_, uint32_t setSize_ );

// Schedule the task with a minimum range.
// This should be set to a value which results in computation effort of at least 10k
// clock cycles to minimize task scheduler overhead.
// NOTE: The last partition will be smaller than m_MinRange if m_SetSize is not a multiple
// of m_MinRange.
// Also known as grain size in literature.
ENKITS_API void                enkiAddTaskSetMinRange( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_,
                                                  void* pArgs_, uint32_t setSize_, uint32_t minRange_ );
// Check if TaskSet is complete. Doesn't wait. Returns 1 if complete, 0 if not.
ENKITS_API int                 enkiIsTaskSetComplete( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ );

// Wait for a given task.
// should only be called from thread which created the task scheduler, or within a task
// if called with 0 it will try to run tasks, and return if none available.
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForTaskSet( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ );

// enkiWaitForTaskSetPriority as enkiWaitForTaskSet but only runs other tasks with priority <= maxPriority_
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForTaskSetPriority( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_, int maxPriority_ );


/* ----------------------------   PinnedTasks   ---------------------------- */
// Create a pinned task.
ENKITS_API enkiPinnedTask*     enkiCreatePinnedTask( enkiTaskScheduler* pETS_, enkiPinnedTaskExecute taskFunc_, uint32_t threadNum_  );

// Delete a pinned task.
ENKITS_API void                enkiDeletePinnedTask( enkiTaskScheduler* pETS_, enkiPinnedTask* pPinnedTask_ );

// Get task parameters via enkiParamsTaskSet
ENKITS_API struct enkiParamsPinnedTask enkiGetParamsPinnedTask( enkiPinnedTask* pTask_ );

// Set task parameters via enkiParamsTaskSet
ENKITS_API void                enkiSetParamsPinnedTask( enkiPinnedTask* pTask_, struct enkiParamsPinnedTask params_ );

// Set PinnedTask ( 0 to ENKITS_TASK_PRIORITIES_NUM-1, where 0 is highest)
ENKITS_API void                enkiSetPriorityPinnedTask( enkiPinnedTask* pTask_, int priority_ );

// Set PinnedTask args
ENKITS_API void                enkiSetArgsPinnedTask( enkiPinnedTask* pTask_, void* pArgs_ );

// Schedule a pinned task
// Pinned tasks can be added from any thread
ENKITS_API void                enkiAddPinnedTask( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_ );

// Schedule a pinned task
// Pinned tasks can be added from any thread
// overwrites args previously set with enkiSetArgsPinnedTask
ENKITS_API void                enkiAddPinnedTaskArgs( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_,
                                           void* pArgs_ );

// This function will run any enkiPinnedTask* for current thread, but not run other
// Main thread should call this or use a wait to ensure its tasks are run.
ENKITS_API void                enkiRunPinnedTasks( enkiTaskScheduler * pETS_ );

// Check if enkiPinnedTask is complete. Doesn't wait. Returns 1 if complete, 0 if not.
ENKITS_API int                 enkiIsPinnedTaskComplete( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_ );

// Wait for a given pinned task.
// should only be called from thread which created the task scheduler, or within a task
// if called with 0 it will try to run tasks, and return if none available.
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForPinnedTask( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_ );

// enkiWaitForPinnedTaskPriority as enkiWaitForPinnedTask but only runs other tasks with priority <= maxPriority_
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForPinnedTaskPriority( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_, int maxPriority_ );

// Waits for the current thread to receive a PinnedTask
// Will not run any tasks - use with RunPinnedTasks()
// Can be used with both ExternalTaskThreads or with an enkiTS tasking thread to create
// a thread which only runs pinned tasks. If enkiTS threads are used can create
// extra enkiTS task threads to handle non blocking computation via normal tasks.
ENKITS_API void                enkiWaitForNewPinnedTasks( enkiTaskScheduler* pETS_ );


/* ----------------------------  Completables  ---------------------------- */
// Get a pointer to an enkiCompletable from an enkiTaskSet.
// Do not call enkiDeleteCompletable on the returned pointer.
ENKITS_API enkiCompletable*    enkiGetCompletableFromTaskSet(    enkiTaskSet* pTaskSet_ );

// Get a pointer to an enkiCompletable from an enkiPinnedTask.
// Do not call enkiDeleteCompletable on the returned pointer.
ENKITS_API enkiCompletable*    enkiGetCompletableFromPinnedTask( enkiPinnedTask* pPinnedTask_ );

// Get a pointer to an enkiCompletable from an enkiPinnedTask.
// Do not call enkiDeleteCompletable on the returned pointer.
ENKITS_API enkiCompletable*    enkiGetCompletableFromCompletionAction( enkiCompletionAction* pCompletionAction_ );

// Create an enkiCompletable
// Can be used with dependencies to wait for their completion.
// Delete with enkiDeleteCompletable
ENKITS_API enkiCompletable*    enkiCreateCompletable( enkiTaskScheduler* pETS_ );

// Delete an enkiCompletable created with enkiCreateCompletable
ENKITS_API void                enkiDeleteCompletable( enkiTaskScheduler* pETS_, enkiCompletable* pCompletable_ );

// Wait for a given completable.
// should only be called from thread which created the task scheduler, or within a task
// if called with 0 it will try to run tasks, and return if none available.
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForCompletable( enkiTaskScheduler* pETS_, enkiCompletable* pTask_ );

// enkiWaitForCompletablePriority as enkiWaitForCompletable but only runs other tasks with priority <= maxPriority_
// Only wait for child tasks of the current task otherwise a deadlock could occur.
ENKITS_API void                enkiWaitForCompletablePriority( enkiTaskScheduler* pETS_, enkiCompletable* pTask_, int maxPriority_ );


/* ----------------------------   Dependencies  ---------------------------- */
// Create an enkiDependency, used to set dependencies between tasks
// Call enkiDeleteDependency to delete.
ENKITS_API enkiDependency*     enkiCreateDependency( enkiTaskScheduler* pETS_ );

// Delete an enkiDependency created with enkiCreateDependency.
ENKITS_API void                enkiDeleteDependency( enkiTaskScheduler* pETS_, enkiDependency* pDependency_ );

// Set a dependency between pDependencyTask_ and pTaskToRunOnCompletion_
// such that when all dependencies of pTaskToRunOnCompletion_ are completed it will run.
ENKITS_API void                enkiSetDependency(
                                    enkiDependency*  pDependency_,
                                    enkiCompletable* pDependencyTask_,
                                    enkiCompletable* pTaskToRunOnCompletion_ );

/* -------------------------- Completion Actions --------------------------- */
// Create a CompletionAction.
// completionFunctionPreComplete_ - function called BEFORE the complete action task is 'complete', which means this is prior to dependent tasks being run.
//                                  this function can thus alter any task arguments of the dependencies.
// completionFunctionPostComplete_ - function called AFTER the complete action task is 'complete'. Dependent tasks may have already been started.
//                                  This function can delete the completion action if needed as it will no longer be accessed by other functions.
// It is safe to set either of these to NULL if you do not require that function
ENKITS_API enkiCompletionAction* enkiCreateCompletionAction( enkiTaskScheduler* pETS_, enkiCompletionFunction completionFunctionPreComplete_, enkiCompletionFunction completionFunctionPostComplete_ );

// Delete a CompletionAction.
ENKITS_API void                enkiDeleteCompletionAction( enkiTaskScheduler* pETS_, enkiCompletionAction* pCompletionAction_ );

// Get task parameters via enkiParamsTaskSet
ENKITS_API struct enkiParamsCompletionAction enkiGetParamsCompletionAction( enkiCompletionAction* pCompletionAction_ );

// Set task parameters via enkiParamsTaskSet
ENKITS_API void                enkiSetParamsCompletionAction( enkiCompletionAction* pCompletionAction_, struct enkiParamsCompletionAction params_ );


#ifdef __cplusplus
}
#endif
