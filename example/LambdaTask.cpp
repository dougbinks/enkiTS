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

#include "TaskScheduler.h"
#include "Timer.h"

#include <stdio.h>
#include <inttypes.h>

#ifndef _WIN32
    #include <string.h>
#endif


// lambda example

int main(int argc, const char * argv[])
{
    enki::TaskScheduler g_TS;
    g_TS.Initialize();

    enki::TaskSet task( 1024, []( enki::TaskSetPartition range, uint32_t threadnum  ) { printf("Thread %d, start %d, end %d\n", threadnum, range.start, range.end ); } );

    g_TS.AddTaskSetToPipe( &task );
    g_TS.WaitforTask( &task );

    return 0;
}
