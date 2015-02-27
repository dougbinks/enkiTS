# enkiTS

## enki Task Scheduler

A permissively licensed C and C++ Task Scheduler for creating parallel programs.

* [C API via src/TaskScheduler_c.h](src/TaskScheduler_c.h)
* [C++ API via src/TaskScheduler.h](src/TaskScheduler.h)
* [C++ 11 version  on Branch C++11](https://github.com/dougbinks/enkiTS/tree/C++11)

Note - this is a work in progress conversion from my code for [enkisoftware's](http://www.enkisoftware.com/) Avoyd codebase, with [RuntimeCompiledC++](https://github.com/RuntimeCompiledCPlusPlus/RuntimeCompiledCPlusPlus) removed along with the removal of profiling code.

As this was originally written before widespread decent C++11 support for atomics and threads, these are implemented here per-platform only supporting Windows, Linux and OSX on Intel x86 / x64. [A separate C++11 branch exists](https://github.com/dougbinks/enkiTS/tree/C++11) for those who would like to use it, but this currently has slightly slower performance under very high task throughput when there is low work per task.

The example code requires C++ 11 for chrono (and for [C++ 11 features in the C++11 branch C++11](https://github.com/dougbinks/enkiTS/tree/C++11) )

## Project Goals

1. *Lightweight* - enkiTS is designed to be lean so you can use it anywhere easily, and understand it.
1. *Fast, then scalable* - enkiTS is designed for consumer devices first, so performance on a low number of threads is important, followed by scalability.
1. *Braided parallelism* - enkiTS can issue tasks from another task as well as from the thread which created the Task System.
1. *Up-front Allocation friendly* - enkiTS is designed for zero allocations during scheduling.

## To Do

* Documentation.
* Add a profile header for support of various profiling libraries such as [ITT](https://software.intel.com/en-us/articles/intel-itt-api-open-source), [Remotery](https://github.com/dougbinks/Remotery), [InsightProfile](https://github.com/kayru/insightprofiler), [MicroProfile](https://bitbucket.org/jonasmeyer/microprofile) and potentially [Telemetry](http://www.radgametools.com/telemetry.htm).
* Benchmarking?


## License (zlib)

Copyright (c) 2013 Doug Binks

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




