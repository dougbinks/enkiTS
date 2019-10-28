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
#include "TaskScheduler.h"

#include <assert.h>

using namespace enki;

struct enkiTaskScheduler : TaskScheduler
{
};

struct enkiTaskSet : ITaskSet
{
    enkiTaskSet( enkiTaskExecuteRange taskFun_ ) : taskFun(taskFun_), pArgs(NULL) {}

    virtual void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_  )
    {
        taskFun( range_.start, range_.end, threadnum_, pArgs );
    }

    enkiTaskExecuteRange taskFun;
    void* pArgs;
};

struct enkiPinnedTask : IPinnedTask
{
    enkiPinnedTask( enkiPinnedTaskExecute taskFun_, uint32_t threadNum_ )
        : IPinnedTask( threadNum_ ), taskFun(taskFun_), pArgs(NULL) {}

    virtual void Execute()
    {
        taskFun( pArgs );
    }

    enkiPinnedTaskExecute taskFun;
    void* pArgs;
};

enkiTaskScheduler* enkiNewTaskScheduler()
{
    enkiTaskScheduler* pETS = new enkiTaskScheduler();
    return pETS;
}

ENKITS_API struct enkiTaskSchedulerConfig enkiGetTaskSchedulerConfig( enkiTaskScheduler* pETS_ )
{
    TaskSchedulerConfig config = pETS_->GetConfig();
    enkiTaskSchedulerConfig configC;
    configC.numExternalTaskThreads                             = config.numExternalTaskThreads;
    configC.numTaskThreadsToCreate                             = config.numTaskThreadsToCreate;
    configC.profilerCallbacks.threadStart                      = config.profilerCallbacks.threadStart;                      
    configC.profilerCallbacks.threadStop                       = config.profilerCallbacks.threadStop;                       
    configC.profilerCallbacks.waitForNewTaskSuspendStart       = config.profilerCallbacks.waitForNewTaskSuspendStart;      
    configC.profilerCallbacks.waitForNewTaskSuspendStop        = config.profilerCallbacks.waitForNewTaskSuspendStop;        
    configC.profilerCallbacks.waitForTaskCompleteStart         = config.profilerCallbacks.waitForTaskCompleteStart;         
    configC.profilerCallbacks.waitForTaskCompleteStop          = config.profilerCallbacks.waitForTaskCompleteStop;          
    configC.profilerCallbacks.waitForTaskCompleteSuspendStart  = config.profilerCallbacks.waitForTaskCompleteSuspendStart;  
    configC.profilerCallbacks.waitForTaskCompleteSuspendStop   = config.profilerCallbacks.waitForTaskCompleteSuspendStop;   
    return configC;
}

void enkiInitTaskScheduler(  enkiTaskScheduler* pETS_ )
{
    pETS_->Initialize();
}

void enkiInitTaskSchedulerNumThreads(  enkiTaskScheduler* pETS_, uint32_t numThreads_ )
{
    pETS_->Initialize( numThreads_ );
}

ENKITS_API void enkiInitTaskSchedulerWithConfig( enkiTaskScheduler* pETS_, struct enkiTaskSchedulerConfig config_ )
{
    TaskSchedulerConfig config;
    config.numExternalTaskThreads                             = config_.numExternalTaskThreads;
    config.numTaskThreadsToCreate                             = config_.numTaskThreadsToCreate;
    config.profilerCallbacks.threadStart                      = config_.profilerCallbacks.threadStart;                      
    config.profilerCallbacks.threadStop                       = config_.profilerCallbacks.threadStop;                       
    config.profilerCallbacks.waitForNewTaskSuspendStart       = config_.profilerCallbacks.waitForNewTaskSuspendStart;      
    config.profilerCallbacks.waitForNewTaskSuspendStop        = config_.profilerCallbacks.waitForNewTaskSuspendStop;        
    config.profilerCallbacks.waitForTaskCompleteStart         = config_.profilerCallbacks.waitForTaskCompleteStart;         
    config.profilerCallbacks.waitForTaskCompleteStop          = config_.profilerCallbacks.waitForTaskCompleteStop;          
    config.profilerCallbacks.waitForTaskCompleteSuspendStart  = config_.profilerCallbacks.waitForTaskCompleteSuspendStart;  
    config.profilerCallbacks.waitForTaskCompleteSuspendStop   = config_.profilerCallbacks.waitForTaskCompleteSuspendStop;   
    pETS_->Initialize( config );
}

void enkiDeleteTaskScheduler( enkiTaskScheduler* pETS_ )
{
    delete pETS_;
}

enkiTaskSet* enkiCreateTaskSet( enkiTaskScheduler* pETS_, enkiTaskExecuteRange taskFunc_  )
{
    return new enkiTaskSet( taskFunc_ );
}

void enkiDeleteTaskSet( enkiTaskSet* pTaskSet_ )
{
    delete pTaskSet_;
}

void enkiSetPriorityTaskSet( enkiTaskSet* pTaskSet_, int priority_ )
{
    assert( priority_ < ENKITS_TASK_PRIORITIES_NUM );
    pTaskSet_->m_Priority = TaskPriority( priority_ );
}

void enkiAddTaskSetToPipe( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_, void* pArgs_, uint32_t setSize_ )
{
    assert( pTaskSet_ );
    assert( pTaskSet_->taskFun );

    pTaskSet_->m_SetSize = setSize_;
    pTaskSet_->pArgs = pArgs_;
    pETS_->AddTaskSetToPipe( pTaskSet_ );
}

void enkiAddTaskSetToPipeMinRange(enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_, void* pArgs_, uint32_t setSize_, uint32_t minRange_)
{
    assert( pTaskSet_ );
    assert( pTaskSet_->taskFun );

    pTaskSet_->m_SetSize = setSize_;
    pTaskSet_->m_MinRange = minRange_;
    pTaskSet_->pArgs = pArgs_;
    pETS_->AddTaskSetToPipe( pTaskSet_ );
}

int enkiIsTaskSetComplete( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ )
{
    assert( pTaskSet_ );
    return ( pTaskSet_->GetIsComplete() ) ? 1 : 0;
}

enkiPinnedTask* enkiCreatePinnedTask(enkiTaskScheduler* pETS_, enkiPinnedTaskExecute taskFunc_, uint32_t threadNum_)
{
    return new enkiPinnedTask( taskFunc_, threadNum_ );
}

void enkiDeletePinnedTask(enkiPinnedTask* pTaskSet_)
{
    delete pTaskSet_;
}

void enkiSetPriorityPinnedTask( enkiPinnedTask* pTask_, int priority_ )
{
    assert( priority_ < ENKITS_TASK_PRIORITIES_NUM );
    pTask_->m_Priority = TaskPriority( priority_ );
}

void enkiAddPinnedTask(enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_, void* pArgs_)
{
    assert( pTask_ );
    pTask_->pArgs = pArgs_;
    pETS_->AddPinnedTask( pTask_ );
}

void enkiRunPinnedTasks(enkiTaskScheduler* pETS_)
{
    pETS_->RunPinnedTasks();
}

int enkiIsPinnedTaskComplete(enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_)
{
    assert( pTask_ );
    return ( pTask_->GetIsComplete() ) ? 1 : 0;
}

void enkiWaitForTaskSet( enkiTaskScheduler* pETS_, enkiTaskSet* pTaskSet_ )
{
    pETS_->WaitforTask( pTaskSet_ );
}

void enkiWaitForTaskSetPriority( enkiTaskScheduler * pETS_, enkiTaskSet * pTaskSet_, int maxPriority_ )
{
    pETS_->WaitforTask( pTaskSet_, TaskPriority( maxPriority_ ) );
}

void enkiWaitForPinnedTask( enkiTaskScheduler* pETS_, enkiPinnedTask* pTask_ )
{
    pETS_->WaitforTask( pTask_ );
}

void enkiWaitForPinnedTaskPriority( enkiTaskScheduler * pETS_, enkiPinnedTask * pTask_, int maxPriority_ )
{
    pETS_->WaitforTask( pTask_, TaskPriority( maxPriority_ ) );
}

void enkiWaitForAll( enkiTaskScheduler* pETS_ )
{
    pETS_->WaitforAll();
}

uint32_t enkiGetNumTaskThreads( enkiTaskScheduler* pETS_ )
{
    return pETS_->GetNumTaskThreads();
}

ENKITS_API uint32_t enkiGetThreadNum( enkiTaskScheduler* pETS_ )
{
    return pETS_->GetThreadNum();
}

ENKITS_API int enkiRegisterExternalTaskThread( enkiTaskScheduler* pETS_)
{
    return (int)pETS_->RegisterExternalTaskThread();
}

ENKITS_API void enkiDeRegisterExternalTaskThread( enkiTaskScheduler* pETS_)
{
    return pETS_->DeRegisterExternalTaskThread();
}

ENKITS_API uint32_t enkiGetNumRegisteredExternalTaskThreads( enkiTaskScheduler* pETS_)
{
    return pETS_->GetNumRegisteredExternalTaskThreads();
}

enkiProfilerCallbacks*    enkiGetProfilerCallbacks( enkiTaskScheduler* pETS_ )
{
    static_assert( sizeof(enkiProfilerCallbacks) == sizeof(enki::ProfilerCallbacks), "enkiTS profiler callback structs do not match" );
    return (enkiProfilerCallbacks*)pETS_->GetProfilerCallbacks();
}

