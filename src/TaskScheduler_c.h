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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct enkiTaskScheduler enkiTaskScheduler;
typedef struct enkiTaskSet		 enkiTaskSet;

typedef void (* enkiTaskExecuteRange)( uint32_t start_, uint32_t end, uint32_t threadnum_, void* pArgs_ );


// Create a task scheduler - will create GetNumHardwareThreads()-1 threads, which is
// sufficient to fill the system when including the main thread.
// Initialize can be called multiple times - it will wait for completion
// before re-initializing.
enkiTaskScheduler*	enkiCreateTaskScheduler();

// Create a task scheduler with numThreads_ (must be > 0)
// will create numThreads_-1 threads, as thread 0 is
// the thread on which the initialize was called.
enkiTaskScheduler*	enkiCreateTaskSchedulerNumThreads( uint32_t numThreads_ );


// Delete a task scheduler
void				enkiDeleteTaskScheduler( enkiTaskScheduler* pETS_ );

// Create a task set.
enkiTaskSet*		enkiCreateTaskSet( enkiTaskScheduler* pETS_, enkiTaskExecuteRange taskFunc_  );

// Delete a task set.
void                enkiDeleteTaskSet( enkiTaskSet* pTaskSet_ );

// schedule the task
void				enkiAddTaskSetToPipe( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_, void* pArgs_, uint32_t setSize_ );

// Check if TaskSet is complete. Doesn't wait. Returns 1 if complete, 0 if not.
int					enkiIsTaskSetComplete( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ );


// Wait for a given task.
// should only be called from thread which created the taskscheduler , or within a task
// if called with 0 it will try to run tasks, and return if none available.
void				enkiWaitForTaskSet( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ );


// Waits for all task sets to complete - not guaranteed to work unless we know we
// are in a situation where tasks aren't being continuosly added.
void				enkiWaitForAll( enkiTaskScheduler* pETS_ );


// get number of threads
uint32_t			enkiGetNumTaskThreads( enkiTaskScheduler* pETS_ );



#ifdef __cplusplus
}
#endif