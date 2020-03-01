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

#ifndef _WIN32
    #include <string.h>
#endif

using namespace enki;

TaskScheduler g_TS;


#include <vector>

// We use a task to launch the first task in the task graph
// as adding the tasks with many dependencies incurs an overhead,
// which we might not want to occur on the main thread
struct TaskLauncher : ITaskSet
{
    ITaskSet*           m_pTaskToLaunch = NULL;
    void ExecuteRange( TaskSetPartition range, uint32_t threadnum ) override
    {
        (void)range;
        g_TS.AddTaskSetToPipe( m_pTaskToLaunch );
    }
};

struct TaskA : ITaskSet
{
    void ExecuteRange( TaskSetPartition range, uint32_t threadnum ) override
    {
        (void)range;
        printf("A on thread %u\n", threadnum);
    }
};

struct TaskB : ITaskSet
{
    Dependency          m_Dependency;

    void ExecuteRange( TaskSetPartition range, uint32_t threadnum ) override
    {
        (void)range;
        printf("B on thread %u\n", threadnum);
    }
};

struct TaskC : IPinnedTask
{
    Dependency          m_Dependencies[4];

    void Execute() override
    {
        printf("C Pinned task on thread %u, should be %u\n", g_TS.GetThreadNum(), threadNum );
    }
};

struct TaskD : ITaskSet
{
    Dependency m_Dependency;

    void ExecuteRange( TaskSetPartition range, uint32_t threadnum ) override
    {
        (void)range;
        printf("D on thread %u\n", threadnum);
    }
};

// If you need to wait on multiple dependencies, but don't need to do anything
// you can derive from ICompletable and add depedencies
struct TasksFinished : ICompletable
{
    std::vector<Dependency>  m_Dependencies;
    Dependency               m_DepencyOnLauncher; // could also store this in array above
};

static const int RUNS       = 20;

int main(int argc, const char * argv[])
{
    g_TS.Initialize();

    // construct the graph once
    TaskA taskA;

    TaskB taskBs[4];
    for( auto& task : taskBs )
    {
        task.SetDependency(task.m_Dependency,&taskA);
    }

    TaskC taskC; // Task C is a pinned task, defaults to running on thread 0 (this thread)
    taskC.SetDependenciesArr( taskC.m_Dependencies, taskBs );

    TaskD taskDs[10];
    for( auto& task : taskDs )
    {
        task.SetDependency(task.m_Dependency,&taskC);
    }

    TasksFinished tasksFinished;
    tasksFinished.SetDependenciesVec( tasksFinished.m_Dependencies, taskDs );

    TaskLauncher taskLauncher;
    taskLauncher.m_pTaskToLaunch = &taskA; //start with task A

    // we need to add a dependency on the launcher to tasksFinished otherwise
    // tasksFinished might be labelled as complete before taskA launched
    tasksFinished.SetDependency( tasksFinished.m_DepencyOnLauncher, &taskLauncher );

    // run graph many times
    for( int run = 0; run< RUNS; ++run )
    {
        printf("Run %d / %d.....\n", run+1, RUNS);

        g_TS.AddTaskSetToPipe( &taskLauncher );

        g_TS.WaitforTask( &tasksFinished );
        printf("Tasks Finished\n");
    }

    return 0;
}
