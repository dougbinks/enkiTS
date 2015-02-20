# enkiTS

## enkiTaskSystem

A C++ Task System.

Note - this is a work in progress conversion from my code for [enkisoftware's](http://www.enkisoftware.com/) Avoyd codebase, with [RuntimeCompiledC++](https://github.com/RuntimeCompiledCPlusPlus/RuntimeCompiledCPlusPlus) removed along with the removal of profiling code.

As this was originally written before widespread decent C++11 support for atomics and threads, these are implemented here per-platform only supporting Windows, Linux and OSX on Intel x86 / x64.

Very much a work in progress ATM, first commit only Windows only build and tested so far.

The example code requires C++ 11 for chrono.

## To Do

* Complete build, test and fix for Linux and OSX.
* Add a profile header for support of various profiling libraries such as [ITT](https://software.intel.com/en-us/articles/intel-itt-api-open-source), [Remotery](https://github.com/dougbinks/Remotery), [InsightProfile](https://github.com/kayru/insightprofiler), [MicroProfile](https://bitbucket.org/jonasmeyer/microprofile) and potentially [Telemetry](http://www.radgametools.com/telemetry.htm).
* Potential C++ 11 conversion (if performance the same)?
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




