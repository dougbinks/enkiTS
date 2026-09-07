Support development of enkiTS through [Github Sponsors](https://github.com/sponsors/dougbinks) or [Patreon](https://www.patreon.com/enkisoftware)

[<img src="https://img.shields.io/static/v1?logo=github&label=Github&message=Sponsor&color=#ea4aaa" width="200"/>](https://github.com/sponsors/dougbinks)    [<img src="https://c5.patreon.com/external/logo/become_a_patron_button@2x.png" alt="Become a Patron" width="150"/>](https://www.patreon.com/enkisoftware)

![enkiTS Logo](https://github.com/dougbinks/images/blob/master/enkiTS_logo_no_padding.png?raw=true)
# enkiTS
| [Master branch](https://github.com/dougbinks/enkiTS/) | [Dev branch](https://github.com/dougbinks/enkiTS/tree/dev) |
| --- | --- |
| [![Build Status for branch: master](https://github.com/dougbinks/enkiTS/actions/workflows/build.yml/badge.svg)](https://github.com/dougbinks/enkiTS/actions) | [![Build Status for branch: dev](https://github.com/dougbinks/enkiTS/actions/workflows/build.yml/badge.svg?branch=dev)](https://github.com/dougbinks/enkiTS/actions) |

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

Building enkiTS is simple, just add the files in enkiTS/src to your build system (_c.* files can be ignored if you only need C++ interface), and add enkiTS/src to your include path. Unix / Linux builds will likely require the pthreads library.

For C++

  - Use `#include "TaskScheduler.h"`
  - Add enkiTS/src to your include path
  - Compile / Add to project: 
    - `TaskScheduler.cpp`
  - Unix / Linux builds will likely require the pthreads library.

For C

  - Use `#include "TaskScheduler_c.h"`
  - Add enkiTS/src to your include path
  - Compile / Add to project:
    - `TaskScheduler.cpp`
    - `TaskScheduler_c.cpp`
  - Unix / Linux builds will likely require the pthreads library.

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
1. *Dependencies* - can set dependendencies between tasks see [example/Dependencies.cpp](example/Dependencies.cpp) and [example/Dependencies_c.c](example/Dependencies_c.c).
1. *Completion Actions* - can perform an action on task completion. This avoids the expensive action of adding the task to the scheduler, and can be used to safely delete a completed task. See [example/CompletionAction.cpp](example/CompletionAction.cpp) and [example/CompletionAction_c.c](example/CompletionAction_c.c)
1. **NEW** *Can wait for pinned tasks* - Can wait for pinned tasks, useful for creating IO threads which do no other work. See [example/WaitForNewPinnedTasks.cpp](example/WaitForNewPinnedTasks.cpp) and [example/WaitForNewPinnedTasks_c.c](example/WaitForNewPinnedTasks_c.c).

## Installing

I recommend using enkiTS directly from source in each project rather than installing it for system wide use. However enkiTS' cmake script can also be used to install the library
if the `ENKITS_INSTALL` cmake variable is set to `ON` (it defaults to `OFF`).

When installed the header files are installed in a subdirectory of the include path, `include/enkiTS` to ensure that they do not conflict with header files from other packages.
When building applications either ensure this is part of the `INCLUDE_PATH` variable or ensure that enkiTS is in the header path in the source files, for example use `#include "enkiTS/TaskScheduler.h"` instead of `#include "TaskScheduler.h"`.

## Using enkiTS

### C++ usage
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
    // since we've just added it and it has no range we'll likely run it.
    g_TS.WaitforTask( &task );
    return 0;
}
```

### C++ 11 lambda usage
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

### Task priorities usage in C++
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

### Pinned Tasks usage in C++
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

### Dependency usage in C++
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

### External task thread usage in C++
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

### WaitForPinnedTasks thread usage in C++ (useful for IO threads)
- full example in [example/WaitForNewPinnedTasks.cpp](example/WaitForNewPinnedTasks.cpp)
- C example in [example/WaitForNewPinnedTasks_c.c](example/WaitForNewPinnedTasks_c.c)
```C++
#include "TaskScheduler.h"

enki::TaskScheduler g_TS;

struct RunPinnedTaskLoopTask : enki::IPinnedTask
{
    void Execute() override
    {
        while( !g_TS.GetIsShutdownRequested() )
        {
            g_TS.WaitForNewPinnedTasks(); // this thread will 'sleep' until there are new pinned tasks
            g_TS.RunPinnedTasks();
        }
    }
};

struct PretendDoFileIO : enki::IPinnedTask
{
    void Execute() override
    {
        // Do file IO
    }
};

int main(int argc, const char * argv[])
{
    enki::TaskSchedulerConfig config;

    // In this example we create more threads than the hardware can run,
    // because the IO thread will spend most of it's time idle or blocked
    // and therefore not scheduled for CPU time by the OS
    config.numTaskThreadsToCreate += 1;

    g_TS.Initialize( config );

    // in this example we place our IO threads at the end
    RunPinnedTaskLoopTask runPinnedTaskLoopTasks;
    runPinnedTaskLoopTasks.threadNum = g_TS.GetNumTaskThreads() - 1;
    g_TS.AddPinnedTask( &runPinnedTaskLoopTasks );

    // Send pretend file IO task to external thread FILE_IO
    PretendDoFileIO pretendDoFileIO;
    pretendDoFileIO.threadNum = runPinnedTaskLoopTasks.threadNum;
    g_TS.AddPinnedTask( &pretendDoFileIO );

    // ensure runPinnedTaskLoopTasks complete by explicitly calling shutdown
    g_TS.WaitforAllAndShutdown();

    return 0;
}
```


## Bindings

- Odin [enkiTS Odin bindings](https://github.com/nadako/odin-enkiTS) by @nadako

## Deprecated

[The C++98 compatible branch](https://github.com/dougbinks/enkiTS/tree/C++98) has been deprecated as I'm not aware of anyone needing it.

The user thread versions are no longer being maintained as they are no longer in use. Similar functionality can be obtained with the externalTaskThreads
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

![ToyPathTracer screenshot](https://github.com/aras-p/ToyPathTracer/blob/main/Shots/screenshot.jpg?raw=true).

### [Mastering Graphics Programming with Vulkan](https://github.com/PacktPublishing/Mastering-Graphics-Programming-with-Vulkan)
Marco Castorina and Gabriel Sassone's book on developing a modern rendering engine from first principles using the Vulkan API. enkiTS is used as the task library to distribute work across cores.

![Mastering Graphics Programming with Vulkan](https://static.packt-cdn.com/products/9781803244792/cover/smaller)

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


## 🌐 Web Resources & Interactive Index
- [MOSCOW METRO DRIVER 3D](https://learnaction.github.io/moscow-metro-driver-3d.html)
- [CATEGORY MOUSE](https://learnaction.netlify.app/category-mouse.html)
- [MAX CRUSHER CRAZY DESTRUCTION AND CAR CRASHES](https://learnaction.netlify.app/max-crusher-crazy-destruction-and-car-crashes.html)
- [SOLVE THE CUBE WOODEN BLOCKS 2D](https://learnaction.netlify.app/solve-the-cube-wooden-blocks-2d.html)
- [CATEGORY CONTROLLER 2](https://learnaction.netlify.app/category-controller-2.html)
- [COSMO VOID](https://learnaction.netlify.app/cosmo-void.html)
- [SITEMAP](https://ptskillcrafts.pages.dev/sitemap.html)
- [PRIVACY](https://learnquester.pages.dev/privacy.html)
- [STICKMAN LEAVE PRISON](https://learnaction.netlify.app/stickman-leave-prison.html)
- [CATEGORY SURVIVAL](https://welearnaction.onrender.com/category-survival.html)
- [URUS CITY DRIVER](https://learnaction.netlify.app/urus-city-driver.html)
- [ONLINE PORTAL](https://quizverses.github.io/)
- [CATEGORY STICKMAN](https://learnaction.netlify.app/category-stickman.html)
- [HUGGY WUGGY GUESS THE RIGHT DOOR](https://learnaction.netlify.app/huggy-wuggy-guess-the-right-door.html)
- [MONSTER MERGE LEGENDS ALIVE](https://learnaction.netlify.app/monster-merge-legends-alive.html)
- [TERMS](https://welearnaction.onrender.com/terms.html)
- [CATEGORY MANAGEMENT210](https://learnaction.netlify.app/category-management210.html)
- [CAR COLLISION MASTER](https://learnaction.netlify.app/car-collision-master.html)
- [CATEGORY MERGE221](https://welearnaction.onrender.com/category-merge221.html)
- [CATEGORY MAGIC46](https://welearnaction.onrender.com/category-magic46.html)
- [CATEGORY MINIGAMES29](https://welearnaction.onrender.com/category-minigames29.html)
- [ONLINE PORTAL](https://brainquests.pages.dev/)
- [PRIVACY](https://cryptotify.pages.dev/privacy.html)
- [CATEGORY TOWER DEFENSE](https://welearnaction.onrender.com/category-tower-defense.html)
- [CATEGORY HORROR 2](https://learnaction.netlify.app/category-horror-2.html)
- [TERMS](https://cryptotify.github.io/terms.html)
- [TERMS](https://brainquests.netlify.app/terms.html)
- [GET TO THE CHOPPER](https://learnaction.netlify.app/get-to-the-chopper.html)
- [CAFE OWNER BUSINESS SIMULATOR](https://learnaction.netlify.app/cafe-owner-business-simulator.html)
- [OFFROAD LIFE 3D](https://learnaction.netlify.app/offroad-life-3d.html)
- [TYPING ADVENTURE](https://learnaction.netlify.app/typing-adventure.html)
- [SERIOUS HEAD](https://learnaction.netlify.app/serious-head.html)
- [2048 MERGE WORLD](https://learnaction.netlify.app/2048-merge-world.html)
- [TERMS](https://ilearnworldjp.pages.dev/terms.html)
- [JAB JAB BOXING](https://learnaction.netlify.app/jab-jab-boxing.html)
- [CATEGORY CUTE](https://learnaction.netlify.app/category-cute.html)
- [CRYPTOGRAM](https://learnaction.netlify.app/cryptogram.html)
- [AIR STRIKE 2D](https://learnaction.netlify.app/air-strike-2d.html)
- [HALLOWEEN FRUIT SLICE](https://learnaction.netlify.app/halloween-fruit-slice.html)
- [CATEGORY ADVENTURE](https://learnaction.netlify.app/category-adventure.html)
- [SOFT GIRLS WINTER AESTHETICS](https://learnaction.netlify.app/soft-girls-winter-aesthetics.html)
- [PANDA ADVENTURE](https://learnaction.netlify.app/panda-adventure.html)
- [SITEMAP](https://brainquests.onrender.com/sitemap.html)
- [FREDDYS NIGHTMARES RETURN HORROR NEW YEAR](https://learnaction.netlify.app/freddys-nightmares-return-horror-new-year.html)
- [CATEGORY FOOD](https://welearnaction.onrender.com/category-food.html)
- [TEACHER SIMULATOR CHRISTMAS EXAM](https://learnaction.netlify.app/teacher-simulator-christmas-exam.html)
- [STICKMAN DUO ESCAPE THE TOMB](https://welearnaction.onrender.com/stickman-duo-escape-the-tomb.html)
- [CATEGORY MOUSE1 697](https://welearnaction.onrender.com/category-mouse1-697.html)
- [SITEMAP](https://ilearnworld.github.io/sitemap.html)
- [CONTACT](https://welearnaction.onrender.com/contact.html)
- [TERMS](https://brainquests.onrender.com/terms.html)
- [ONLINE PORTAL](https://brainquests.netlify.app/)
- [HOME RUSH THE FISH WAR](https://learnaction.netlify.app/home-rush-the-fish-war.html)
- [SITEMAP](https://brainquests.netlify.app/sitemap.html)
- [HAMSTERCYCLE](https://learnaction.netlify.app/hamstercycle.html)
- [HALLOWEEN STICKMAN](https://learnaction.netlify.app/halloween-stickman.html)
- [CATEGORY SPORTS](https://learnaction.netlify.app/category-sports.html)
- [ONLINE PORTAL](https://cryptotify.web.app/)
- [CATEGORY DRIFTING116](https://learnaction.netlify.app/category-drifting116.html)
- [CATEGORY PUZZLE 5](https://welearnaction.onrender.com/category-puzzle-5.html)
- [CYBERPUNK CITY FASHION](https://learnaction.netlify.app/cyberpunk-city-fashion.html)
- [CANDY POP MANIA](https://learnaction.netlify.app/candy-pop-mania.html)
- [TILEMAN IO](https://welearnaction.onrender.com/tileman-io.html)
- [PING PONG AIR](https://learnaction.netlify.app/ping-pong-air.html)
- [PET ME MAZE](https://welearnaction.onrender.com/pet-me-maze.html)
- [ONLINE PORTAL](https://brainquests-fb2c5.web.app/)
- [ONLINE PORTAL](https://skillcrafts.github.io/)
- [BUILD AND RUN](https://learnaction.netlify.app/build-and-run.html)
- [CATEGORY SKILL256](https://welearnaction.onrender.com/category-skill256.html)
- [CATEGORY MINECRAFT81](https://welearnaction.onrender.com/category-minecraft81.html)
- [SITEMAP](https://cryptotify.pages.dev/sitemap.html)
- [CATEGORY COLLECT566](https://welearnaction.onrender.com/category-collect566.html)
- [GATE HEROES BATTLE](https://learnaction.netlify.app/gate-heroes-battle.html)
- [PRIVACY](https://studyquesthub.web.app/privacy.html)
- [TERMS](https://studyquesthub.web.app/terms.html)
- [TERMS](https://quizverses.pages.dev/terms.html)
- [DOGGO DROP](https://learnaction.netlify.app/doggo-drop.html)
- [CATEGORY 3D1 371](https://welearnaction.onrender.com/category-3d1-371.html)
- [PLANT GIRL DEFENSE ZOMBIE](https://welearnaction.onrender.com/plant-girl-defense-zombie.html)
- [SOLITAIRE DELUXE EDITION](https://learnaction.netlify.app/solitaire-deluxe-edition.html)
- [NONOGRAM DAILY](https://learnaction.github.io/nonogram-daily.html)
- [MOJICON WINTER CONNECT](https://learnaction.github.io/mojicon-winter-connect.html)
- [G WAGON CITY DRIVER](https://learnaction.github.io/g-wagon-city-driver.html)
- [ACADEMY ASSAULT](https://learnaction.netlify.app/academy-assault.html)
- [DALGONA MASTER](https://welearnaction.onrender.com/dalgona-master.html)
- [STYLISH NAIL ART](https://welearnaction.onrender.com/stylish-nail-art.html)
- [CATEGORY SOCCER 2](https://learnaction.netlify.app/category-soccer-2.html)
- [DAILY CHESS PUZZLE](https://learnaction.github.io/daily-chess-puzzle.html)
- [OFF ROAD OVERDRIVE](https://learnaction.netlify.app/off-road-overdrive.html)
- [FARM DEFENSE](https://learnaction.netlify.app/farm-defense.html)
- [GOON BALL](https://learnaction.netlify.app/goon-ball.html)
- [CATEGORY MONSTER206](https://learnaction.github.io/category-monster206.html)
- [BUNNIES SORT](https://learnaction.netlify.app/bunnies-sort.html)
- [AUTUMN GLAM GALA](https://learnaction.netlify.app/autumn-glam-gala.html)
- [TRAITOR BEAVER](https://learnaction.github.io/traitor-beaver.html)
- [CATEGORY POINT AND CLICK123](https://learnaction.github.io/category-point-and-click123.html)
- [CATEGORY SKILL256](https://learnaction.github.io/category-skill256.html)
- [STREET RACING MOTO DRIFT](https://learnaction.netlify.app/street-racing-moto-drift.html)
- [PRIVACY](https://cryptotify.web.app/privacy.html)
- [MALL ANOMALY](https://learnaction.netlify.app/mall-anomaly.html)
- [CATEGORY MERGE GAME](https://welearnaction.onrender.com/category-merge-game.html)
- [SPACE STRIKE GALAXY SHOOTER](https://welearnaction.onrender.com/space-strike-galaxy-shooter.html)
- [EXTREME CAR DRIVING SIMULATOR](https://welearnaction.onrender.com/extreme-car-driving-simulator.html)
- [OBBY BLOX HOOK](https://learnaction.github.io/obby-blox-hook.html)
- [RAINBOW BALLS 2048](https://learnaction.github.io/rainbow-balls-2048.html)
- [CATEGORY TANK58](https://learnaction.github.io/category-tank58.html)
- [COLOR BLOCK BLAST 3](https://learnaction.netlify.app/color-block-blast-3.html)
- [CATEGORY LOL41](https://welearnaction.onrender.com/category-lol41.html)
- [MATCH MASTERS](https://learnaction.github.io/match-masters.html)
- [CRAZYSTEVEIO](https://welearnaction.onrender.com/crazysteveio.html)
- [CATEGORY ARMY40](https://learnaction.netlify.app/category-army40.html)
- [BACKROOMS SKIBIDI TERRORS](https://learnaction.netlify.app/backrooms-skibidi-terrors.html)
- [CATEGORY TOWER DEFENSE 2](https://welearnaction.onrender.com/category-tower-defense-2.html)
- [CATEGORY AGILITY 2](https://learnaction.github.io/category-agility-2.html)
- [MINE 2D SURVIVAL HEROBRINE](https://learnaction.github.io/mine-2d-survival-herobrine.html)
- [STICKMAN TEAM DETROIT](https://welearnaction.onrender.com/stickman-team-detroit.html)
- [CUBE CONNECT](https://learnaction.github.io/cube-connect.html)
- [CATEGORY AVOID295](https://welearnaction.onrender.com/category-avoid295.html)
- [ROPE STITCH PUZZLE](https://learnaction.github.io/rope-stitch-puzzle.html)
- [CATEGORY SNAKE](https://learnaction.github.io/category-snake.html)
- [GROW A GARDEN FOR BRAINROTS](https://learnaction.github.io/grow-a-garden-for-brainrots.html)
- [CATEGORY SURVIVAL366](https://welearnaction.onrender.com/category-survival366.html)
- [INDEX21](https://learnaction.github.io/index21.html)
- [COLOR BRAIN TEST GAMES](https://learnaction.netlify.app/color-brain-test-games.html)
- [ASMR WATER VS FIRE](https://learnaction.netlify.app/asmr-water-vs-fire.html)
- [CATEGORY CAR 2](https://learnaction.netlify.app/category-car-2.html)
- [VIRTUAL NEKO KITTY COLLECTOR](https://learnaction.github.io/virtual-neko-kitty-collector.html)
- [SNOW RACE 3D FUN RACING](https://learnaction.netlify.app/snow-race-3d-fun-racing.html)
- [CRAZYZOMBIES 3D](https://learnaction.github.io/crazyzombies-3d.html)
- [WOODS OF NEVIA FOREST SURVIVAL](https://welearnaction.onrender.com/woods-of-nevia-forest-survival.html)
