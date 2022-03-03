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

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#ifndef _WIN32
    #include <string.h>
#endif

using namespace enki;

TaskScheduler g_TS;

static std::atomic<int32_t> gs_CountAsRun = {0};
static std::atomic<int32_t> gs_CountAsDeleted = {0};
static std::atomic<int32_t> gs_CountBsRun = {0};
static std::atomic<int32_t> gs_CountBsDeleted = {0};

struct CompletionActionDelete : ICompletable
{
    Dependency    m_Dependency;

    // We override OnDependenciesComplete to provide an 'action' which occurs after
    // the dependency task is complete.
    void OnDependenciesComplete( TaskScheduler* pTaskScheduler_, uint32_t threadNum_ )
    {
        // Call base class OnDependenciesComplete BEFORE deleting depedent task or self
        ICompletable::OnDependenciesComplete( pTaskScheduler_, threadNum_ );

        printf("CompletionActionDelete::OnDependenciesComplete called on thread %u\n", threadNum_ );

        // In this example we delete the dependency, which is safe to do as the task
        // manager will not dereference it at this point.
        // However the dependency task should have no other dependents,
        // This class can have dependencies.
        delete m_Dependency.GetDependencyTask(); // also deletes this as member
    }
};

struct SelfDeletingTaskB : ITaskSet
{
    SelfDeletingTaskB()
    {
        m_TaskDeleter.SetDependency( m_TaskDeleter.m_Dependency, this );
    }

    ~SelfDeletingTaskB()
    {
        ++gs_CountBsDeleted;
        printf("~SelfDeletingTaskB() called on thread %u\n\n", g_TS.GetThreadNum() );
    }

    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
    {
        if( 0 == range_.start )
        {
            // whilst would normally loop over range_ doing work here we want to only output info once per task
            ++gs_CountBsRun;
            printf("SelfDeletingTaskB on thread %u with set size %u\n", threadnum_, m_SetSize);
        }
    }

    CompletionActionDelete m_TaskDeleter;
    Dependency             m_Dependency;
};

struct CompletionActionModifyDependentTaskAndDelete : ICompletable
{
    Dependency    m_Dependency;

    ITaskSet*     m_pTaskToModify = nullptr;

    // We override OnDependenciesComplete to provide an 'action' which occurs after
    // the dependency task is complete.
    void OnDependenciesComplete( TaskScheduler* pTaskScheduler_, uint32_t threadNum_ )
    {
        // Modify following task before calling OnDependenciesComplete
        m_pTaskToModify->m_SetSize = 10;

        // Call base class OnDependenciesComplete AFTER modifying any depedent task
        ICompletable::OnDependenciesComplete( pTaskScheduler_, threadNum_ );

        printf("CompletionActionModifyDependentTaskAndDelete::OnDependenciesComplete called on thread %u\n", threadNum_ );

        // In this example we delete the dependency, which is safe to do as the task
        // manager will not dereference it at this point.
        // However the dependency task should have no other dependents,
        // This class can have dependencies.
        delete m_Dependency.GetDependencyTask(); // also deletes this as member
    }
};

struct SelfDeletingTaskA : ITaskSet
{
    SelfDeletingTaskA()
    {
        m_TaskModifyAndDelete.SetDependency( m_TaskModifyAndDelete.m_Dependency, this );
        SelfDeletingTaskB* pNextTask = new SelfDeletingTaskB();

        // we set the dependency of pNextTask on the task deleter, not on this
        pNextTask->SetDependency( pNextTask->m_Dependency, &m_TaskModifyAndDelete );

        // Set the completion actions task to modify to be the following task
        m_TaskModifyAndDelete.m_pTaskToModify = pNextTask;
    }

    ~SelfDeletingTaskA()
    {
        ++gs_CountAsDeleted;
        printf("~SelfDeletingTaskA() called on thread %u\n\n", g_TS.GetThreadNum() );
    }

    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
    {
        (void)range_;
        ++gs_CountAsRun;
        printf("SelfDeletingTaskA  on thread %u with set size %u\n", threadnum_, m_SetSize);
    }

    CompletionActionModifyDependentTaskAndDelete m_TaskModifyAndDelete;
};

static const int RUNS       = 10;

int main(int argc, const char * argv[])
{
    // This examples shows CompletionActions used to modify a following tasks parameters and delete tasks
    // Task Graph for this example (with names shortened to fit on screen):
    // 
    // pTaskSetA
    //          ->pCompletionActionA-Modify-ICompletable::OnDependenciesComplete-Delete
    //                                     ->pTaskSetB
    //                                                ->pCompletionActionB-ICompletable::OnDependenciesComplete-Delete
    //
    // Note that pTaskSetB must depend on pCompletionActionA NOT pTaskSetA or it could run at the same time as pCompletionActionA
    // so cannot be modified.

    g_TS.Initialize();

    for( int run = 0; run< RUNS; ++run )
    {
        g_TS.AddTaskSetToPipe( new SelfDeletingTaskA() );
    }
    g_TS.WaitforAllAndShutdown();

    printf("%d As run, %d deleted\n%d Bs run, %d deleted.", gs_CountAsRun.load(), gs_CountAsDeleted.load(),  gs_CountBsRun.load(), gs_CountBsDeleted.load() );

    if( gs_CountAsRun != gs_CountAsDeleted ||
        gs_CountBsRun != gs_CountBsDeleted ||
        gs_CountAsRun != gs_CountBsRun )
    {
        printf("ERROR\n");
        return 1;
    }

    return 0;
}
