Support development of enkiTS through our [Patreon](https://www.patreon.com/enkisoftware)

[<img src="https://c5.patreon.com/external/logo/become_a_patron_button@2x.png" alt="Become a Patron" width="150"/>](https://www.patreon.com/enkisoftware)

# enkiTS
[![Build Status for branch: C++11](https://travis-ci.org/dougbinks/enkiTS.svg?branch=C++11)](https://travis-ci.org/dougbinks/enkiTS)

## enki Task Scheduler

A permissively licensed C and C++ Task Scheduler for creating parallel programs. **NEW** The master branch now requires C++11 support, [A C++98 branch exists](https://github.com/dougbinks/enkiTS/tree/C++98) for those without access to a C++11 compiler or for ease in porting to pure C, though it may be deprecated in time.

* [C++ API via src/TaskScheduler.h](src/TaskScheduler.h)
* [C API via src/TaskScheduler_c.h](src/TaskScheduler_c.h)

enkiTS was developed for, and is used in [enkisoftware](http://www.enkisoftware.com/)'s Avoyd codebase.

## Platforms

- Windows, Linux, Mac OS, Android (should work on iOS) 
- x64 & x86, ARM

enkiTS is primarily developed on x64 and x86 Intel architectures on MS Windows, with well tested support for Linux and somewhat less frequently tested support on Mac OS and ARM Android.

## Examples

Several examples exist in  the [example folder](https://github.com/dougbinks/enkiTS/tree/dev_C%2B%2B11/example). The example code requires C++ 11 for chrono.

For further examples, see https://github.com/dougbinks/enkiTSExamples

## Building

Building enkiTS is simple, just add the files in enkiTS/src to your build system (_c.* files can be ignored if you only need C++ interface), and add enkiTS/src to your include path. Unix / Linux builds will require the pthreads library.

For cmake, on Windows / Mac OS X / Linux with cmake installed, open a prompt in the enkiTS directory and:

1. `mkdir build`
2. `cmake ..`
3. either run `make` or open `enkiTS.sln`

## Project Features

1. *Lightweight* - enkiTS is designed to be lean so you can use it anywhere easily, and understand it.
1. *Fast, then scalable* - enkiTS is designed for consumer devices first, so performance on a low number of threads is important, followed by scalability.
1. *Braided parallelism* - enkiTS can issue tasks from another task as well as from the thread which created the Task System.
1. *Up-front Allocation friendly* - enkiTS is designed for zero allocations during scheduling.
1. *Can pin tasks to a given thread* - enkiTS can schedule a task which will only be run on the specified thread.
1. **NEW** - *Can set task priorities* - Up to 5 task priorities can be configured via define ENKITS_TASK_PRIORITIES_NUM (defaults to 3). Higher priority tasks are run before lower priority ones.
 
## Usage

C++ usage:
```C
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

// define a task set, can ignore range if we only do one thing
struct ParallelTaskSet : enki::ITaskSet {
   virtual void    ExecuteRange(  enki::TaskSetPartition range, uint32_t threadnum ) {
      // do something here, can issue tasks with g_TS
   }
};

int main(int argc, const char * argv[]) {
   g_TS.Initialize();
   ParallelTaskSet task; // default constructor has a set size of 1
   g_TS.AddTaskSetToPipe( &task );

   // wait for task set (running tasks if they exist) - since we've just added it and it has no range we'll likely run it.
   g_TS.WaitforTask( &task );
   return 0;
}
```

C++ 11 lambda usage (not available on C++98 branch):
```C
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

int main(int argc, const char * argv[]) {
   g_TS.Initialize();

   enki::TaskSet task( 1, []( enki::TaskSetPartition range, uint32_t threadnum  ) {
         // do something here
      }  );

   g_TS.AddTaskSetToPipe( &task );
   g_TS.WaitforTask( &task );
   return 0;
}
```

Task priorities usage in C++  (see example/Priorities_c.c for C example).
```C
// See full example in Priorities.cpp
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

struct ExampleTask : enki::ITaskSet
{
    ExampleTask( ) { m_SetSize = size_; }

    virtual void ExecuteRange( enki::TaskSetPartition range, uint32_t threadnum ) {
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

Pinned Tasks usage in C++ (see example/PinnedTask_c.c for C example).
```C
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

// define a task set, can ignore range if we only do one thing
struct PinnedTask : enki::IPinnedTask {
    virtual void    Execute() {
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
   // wait for task set (running tasks if they exist) - since we've just added it and it has no range we'll likely run it.
   g_TS.WaitforTask( &task );
   return 0;
}
```


C usage:
```C
#include "TaskScheduler_c.h"

enkiTaskScheduler*	g_pTS;

void ParalleTaskSetFunc( uint32_t start_, uint32_t end, uint32_t threadnum_, void* pArgs_ ) {
   /* Do something here, can issue tasks with g_pTS */
}

int main(int argc, const char * argv[]) {
   enkiTaskSet* pTask;
   g_pTS = enkiNewTaskScheduler();
   enkiInitTaskScheduler( g_pTS );
	
   // create a task, can re-use this to get allocation occurring on startup
   pTask	= enkiCreateTaskSet( g_pTS, ParalleTaskSetFunc );

   enkiAddTaskSetToPipe( g_pTS, pTask, NULL, 1); // NULL args, setsize of 1

   // wait for task set (running tasks if they exist) - since we've just added it and it has no range we'll likely run it.
   enkiWaitForTaskSet( g_pTS, pTask );
   
   enkiDeleteTaskSet( pTask );
   
   enkiDeleteTaskScheduler( g_pTS );
   
   return 0;
}
```

## Bindings

- C# [EnkiTasks C#](https://github.com/nxrighthere/EnkiTasks-CSharp)

## Deprecated

The user thread versions are no longer being maintained as they are no longer in use.
* [User thread version  on Branch UserThread](https://github.com/dougbinks/enkiTS/tree/UserThread) for running enkiTS on other tasking / threading systems, so it can be used as in other engines as well as standalone for example.
* [C++ 11 version of user threads on Branch UserThread_C++11](https://github.com/dougbinks/enkiTS/tree/UserThread_C++11)

## Projects using enkiTS

### [Avoyd](https://www.avoyd.com)
Avoyd is an abstract 6 degrees of freedom voxel game. enkiTS was developed for use in our [in-house engine powering Avoyd](https://www.enkisoftware.com/faq#engine). 

![Avoyd screenshot](https://github.com/juliettef/Media/blob/master/Avoyd_2019-06-22_enkiTS_microprofile.jpg?raw=true)

### [Imogen](https://github.com/CedricGuillemet/Imogen)
GPU/CPU Texture Generator

![Imogen screenshot](https://camo.githubusercontent.com/28347bc0c1627aa4f289e1b2b769afcb3a5de370/68747470733a2f2f692e696d6775722e636f6d2f7351664f3542722e706e67)

### [ToyPathRacer](https://github.com/aras-p/ToyPathTracer)
Aras Pranckevičius' code for his series on [Daily Path Tracer experiments with various languages](https://aras-p.info/blog/2018/03/28/Daily-Pathtracer-Part-0-Intro/).

![ToyPathTracer screenshot](https://github.com/aras-p/ToyPathTracer/blob/master/Shots/screenshot.jpg?raw=true).

## License (zlib)

Copyright (c) 2013-2019 Doug Binks

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
