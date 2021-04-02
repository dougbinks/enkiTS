Support development of enkiTS through [Github Sponsors](https://github.com/sponsors/dougbinks) or [Patreon](https://www.patreon.com/enkisoftware)

[<img src="https://img.shields.io/static/v1?logo=github&label=Github&message=Sponsor&color=#ea4aaa" width="200"/>](https://github.com/sponsors/dougbinks)    [<img src="https://c5.patreon.com/external/logo/become_a_patron_button@2x.png" alt="Become a Patron" width="150"/>](https://www.patreon.com/enkisoftware)

![enkiTS Logo](https://github.com/dougbinks/images/blob/master/enkiTS_logo_no_padding.png?raw=true)
# enkiTS
| [Master branch](https://github.com/dougbinks/enkiTS/) | [Dev branch](https://github.com/dougbinks/enkiTS/tree/dev) |
| --- | --- |
| [![Build Status for branch: master](https://travis-ci.org/dougbinks/enkiTS.svg?branch=master)](https://travis-ci.org/dougbinks/enkiTS) | [![Build Status for branch: dev](https://travis-ci.org/dougbinks/enkiTS.svg?branch=dev)](https://travis-ci.org/dougbinks/enkiTS) |

## enki Task Scheduler

A permissively licensed C and C++ Task Scheduler for creating parallel programs. Requires C++11 support.

The primary goal of enkiTS is to help developers create programs which handle both data and task level parallelism to utilize the full performance of multicore CPUs, whilst being lightweight (only a small amount of code) and easy to use.

* [C++ API via src/TaskScheduler.h](src/TaskScheduler.h)
* [C API via src/TaskScheduler_c.h](src/TaskScheduler_c.h)

enkiTS was developed for, and is used in [enkisoftware](http://www.enkisoftware.com/)'s Avoyd codebase.

## Platforms

- Windows, Linux, Mac OS, Android (should work on iOS) 
- x64 & x86, ARM

enkiTS is primarily developed on x64 and x86 Intel architectures on MS Windows, with well tested support for Linux and somewhat less frequently tested support on Mac OS and ARM Android.

## Examples

Several examples exist in  the [example folder](https://github.com/dougbinks/enkiTS/tree/master/example).

For further examples, see https://github.com/dougbinks/enkiTSExamples

## Building

Building enkiTS is simple, just add the files in enkiTS/src to your build system (_c.* files can be ignored if you only need C++ interface), and add enkiTS/src to your include path. Unix / Linux builds will require the pthreads library.

For cmake, on Windows / Mac OS X / Linux with cmake installed, open a prompt in the enkiTS directory and:

1. `mkdir build`
1. `cd build`
1. `cmake ..`
1. either run `make all` or for Visual Studio open `enkiTS.sln`

## Project Features

1. *Lightweight* - enkiTS is designed to be lean so you can use it anywhere easily, and understand it.
1. *Fast, then scalable* - enkiTS is designed for consumer devices first, so performance on a low number of threads is important, followed by scalability.
1. *Braided parallelism* - enkiTS can issue tasks from another task as well as from the thread which created the Task System, and has a simple task interface for both data parallel and task parallelism.
1. *Up-front Allocation friendly* - enkiTS is designed for zero allocations during scheduling.
1. *Can pin tasks to a given thread* - enkiTS can schedule a task which will only be run on the specified thread.
1. *Can set task priorities* - Up to 5 task priorities can be configured via define ENKITS_TASK_PRIORITIES_NUM (defaults to 3). Higher priority tasks are run before lower priority ones.
1. *Can register external threads to use with enkiTS* - Can configure enkiTS with numExternalTaskThreads which can be registered to use with the enkiTS API.
1. *Custom allocator API* - can configure enkiTS with custom allocators, see [example/CustomAllocator.cpp](example/CustomAllocator.cpp) and [example/CustomAllocator_c.c](example/CustomAllocator_c.c).
1. **NEW** *Dependencies* - can set dependendencies between tasks see [example/Dependencies.cpp](example/Dependencies.cpp) and [example/Dependencies_c.c](example/Dependencies_c.c).
1. **NEW** *Completion Actions* - can perform an action on task completion. This avoids the expensive action of adding the task to the scheduler, and can be used to safely delete a completed task. See [example/CompletionAction.cpp](example/CompletionAction.cpp) and [example/CompletionAction_c.c](example/CompletionAction_c.c)

## Usage

C++ usage:
- full example in [example/ParallelSum.cpp](example/ParallelSum.cpp)
- C example in [example/ParallelSum_c.c](example/ParallelSum_c.c)
```C
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

// define a task set, can ignore range if we only do one thing
struct ParallelTaskSet : enki::ITaskSet {
    void ExecuteRange(  enki::TaskSetPartition range_, uint32_t threadnum_ ) override {
        // do something here, can issue tasks with g_TS
    }
};

int main(int argc, const char * argv[]) {
    g_TS.Initialize();
    ParallelTaskSet task; // default constructor has a set size of 1
    g_TS.AddTaskSetToPipe( &task );

    // wait for task set (running tasks if they exist)
    since we've just added it and it has no range we'll likely run it.
    g_TS.WaitforTask( &task );
    return 0;
}
```

C++ 11 lambda usage:
- full example in [example/LambdaTask.cpp](example/LambdaTask.cpp)
```C
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

int main(int argc, const char * argv[]) {
   g_TS.Initialize();

   enki::TaskSet task( 1, []( enki::TaskSetPartition range_, uint32_t threadnum_  ) {
         // do something here
      }  );

   g_TS.AddTaskSetToPipe( &task );
   g_TS.WaitforTask( &task );
   return 0;
}
```

Task priorities usage in C++:
- full example in [example/Priorities.cpp](example/Priorities.cpp)
- C example in [example/Priorities_c.c](example/Priorities_c.c)
```C
// See full example in Priorities.cpp
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

struct ExampleTask : enki::ITaskSet
{
    ExampleTask( ) { m_SetSize = size_; }

    void ExecuteRange(  enki::TaskSetPartition range_, uint32_t threadnum_ ) override {
        // See full example in Priorities.cpp
    }
};


// This example demonstrates how to run a long running task alongside tasks
// which must complete as early as possible using priorities.
int main(int argc, const char * argv[])
{
    g_TS.Initialize();

    ExampleTask lowPriorityTask( 10 );
    lowPriorityTask.m_Priority  = enki::TASK_PRIORITY_LOW;

    ExampleTask highPriorityTask( 1 );
    highPriorityTask.m_Priority = enki::TASK_PRIORITY_HIGH;

    g_TS.AddTaskSetToPipe( &lowPriorityTask );
    for( int task = 0; task < 10; ++task )
    {
        // run high priority tasks
        g_TS.AddTaskSetToPipe( &highPriorityTask );

        // wait for task but only run tasks of the same priority or higher on this thread
        g_TS.WaitforTask( &highPriorityTask, highPriorityTask.m_Priority );
    }
    // wait for low priority task, run any tasks on this thread whilst waiting
    g_TS.WaitforTask( &lowPriorityTask );

    return 0;
}
```

Pinned Tasks usage in C++:
- full example in [example/PinnedTask.cpp](example/PinnedTask.cpp)
- C example in [example/PinnedTask_c.c](example/PinnedTask_c.c)
```C
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

// define a task set, can ignore range if we only do one thing
struct PinnedTask : enki::IPinnedTask {
    void Execute() override {
      // do something here, can issue tasks with g_TS
    }
};

int main(int argc, const char * argv[]) {
    g_TS.Initialize();
    PinnedTask task; //default constructor sets thread for pinned task to 0 (main thread)
    g_TS.AddPinnedTask( &task );

    // RunPinnedTasks must be called on main thread to run any pinned tasks for that thread.
    // Tasking threads automatically do this in their task loop.
    g_TS.RunPinnedTasks();

    // wait for task set (running tasks if they exist)
    // since we've just added it and it has no range we'll likely run it.
    g_TS.WaitforTask( &task );
    return 0;
}
```

Dependency usage in C++:
- full example in [example/Dependencies.cpp](example/Dependencies.cpp)
- C example in [example/Dependencies_c.c](example/Dependencies_c.c)
```C
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

// define a task set, can ignore range if we only do one thing
struct TaskA : enki::ITaskSet {
    void ExecuteRange(  enki::TaskSetPartition range_, uint32_t threadnum_ ) override {
        // do something here, can issue tasks with g_TS
    }
};

struct TaskB : enki::ITaskSet {
    enki::Dependency m_Dependency;
    void ExecuteRange(  enki::TaskSetPartition range_, uint32_t threadnum_ ) override {
        // do something here, can issue tasks with g_TS
    }
};

int main(int argc, const char * argv[]) {
    g_TS.Initialize();
    
    // set dependencies once (can set more than one if needed).
    TaskA taskA;
    TaskB taskB;
    taskB.SetDependency( taskB.m_Dependency, &taskA );

    g_TS.AddTaskSetToPipe( &taskA ); // add first task
    g_TS.WaitforTask( &taskB );      // wait for last
    return 0;
}
```

External thread usage in C++:
- full example in [example/ExternalTaskThread.cpp](example/ExternalTaskThread.cpp)
- C example in [example/ExternalTaskThread_c.c](example/ExternalTaskThread_c.c)
```C
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;
struct ParallelTaskSet : ITaskSet
{
    void ExecuteRange(  enki::TaskSetPartition range_, uint32_t threadnum_ ) override {
        // Do something
    }
};

void threadFunction()
{
    g_TS.RegisterExternalTaskThread();

    // sleep for a while instead of doing something such as file IO
    std::this_thread::sleep_for( std::chrono::milliseconds( num_ * 100 ) );

    ParallelTaskSet task;
    g_TS.AddTaskSetToPipe( &task );
    g_TS.WaitforTask( &task);

    g_TS.DeRegisterExternalTaskThread();
}

int main(int argc, const char * argv[])
{
    enki::TaskSchedulerConfig config;
    config.numExternalTaskThreads = 1; // we have one extra external thread

    g_TS.Initialize( config );

    std::thread exampleThread( threadFunction );

    exampleThread.join();

    return 0;
}
```

C usage:
```C
#include "TaskScheduler_c.h"

enkiTaskScheduler*	g_pTS;

void ParalleTaskSetFunc( uint32_t start_, uint32_t end_, uint32_t threadnum_, void* pArgs_ ) {
   /* Do something here, can issue tasks with g_pTS */
}

int main(int argc, const char * argv[]) {
   enkiTaskSet* pTask;
   g_pTS = enkiNewTaskScheduler();
   enkiInitTaskScheduler( g_pTS );

   // create a task, can re-use this to get allocation occurring on startup
   pTask = enkiCreateTaskSet( g_pTS, ParalleTaskSetFunc );

   enkiAddTaskSetToPipe( g_pTS, pTask); // defaults are NULL args, setsize of 1

   // wait for task set (running tasks if they exist)
   // since we've just added it and it has no range we'll likely run it.
   enkiWaitForTaskSet( g_pTS, pTask );
   
   enkiDeleteTaskSet( g_pTS, pTask );
   
   enkiDeleteTaskScheduler( g_pTS );
   
   return 0;
}
```


## Bindings

- C# [EnkiTasks C#](https://github.com/nxrighthere/EnkiTasks-CSharp)

## Deprecated

[The C++98 compatible branch](https://github.com/dougbinks/enkiTS/tree/C++98) has been deprecated as I'm not aware of anyone needing it.

The user thread versions are no longer being maintained as they are no longer in use.
* [User thread version  on Branch UserThread](https://github.com/dougbinks/enkiTS/tree/UserThread) for running enkiTS on other tasking / threading systems, so it can be used as in other engines as well as standalone for example.
* [C++ 11 version of user threads on Branch UserThread_C++11](https://github.com/dougbinks/enkiTS/tree/UserThread_C++11)

## Projects using enkiTS

### [Avoyd](https://www.avoyd.com)
Avoyd is an abstract 6 degrees of freedom voxel game. enkiTS was developed for use in our [in-house engine powering Avoyd](https://www.enkisoftware.com/faq#engine). 

![Avoyd screenshot](https://github.com/juliettef/Media/blob/main/Avoyd_2019-06-22_enkiTS_microprofile.jpg?raw=true)

### [Imogen](https://github.com/CedricGuillemet/Imogen)
GPU/CPU Texture Generator

![Imogen screenshot](https://camo.githubusercontent.com/28347bc0c1627aa4f289e1b2b769afcb3a5de370/68747470733a2f2f692e696d6775722e636f6d2f7351664f3542722e706e67)

### [ToyPathRacer](https://github.com/aras-p/ToyPathTracer)
Aras Pranckevičius' code for his series on [Daily Path Tracer experiments with various languages](https://aras-p.info/blog/2018/03/28/Daily-Pathtracer-Part-0-Intro/).

![ToyPathTracer screenshot](https://github.com/aras-p/ToyPathTracer/blob/master/Shots/screenshot.jpg?raw=true).

## License (zlib)

Copyright (c) 2013-2020 Doug Binks

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgement in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
