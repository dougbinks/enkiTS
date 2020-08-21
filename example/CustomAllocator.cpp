// Copyright (c) 2019 Doug Binks
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
#include <thread>

using namespace enki;

TaskScheduler g_TS;

struct ParallelTaskSet : ITaskSet
{
    void ExecuteRange( TaskSetPartition range_, uint32_t threadnum_ ) override
    {
        printf(" This could run on any thread, currently thread %d\n", threadnum_);
    }
};

struct CustomData
{
    const char* domainName;
    size_t totalAllocations;
};

void* CustomAllocFunc( size_t align_, size_t size_, void* userData_, const char* file_, int line_ )
{
    CustomData* data = (CustomData*)userData_;
    data->totalAllocations += size_;

    // We don't need to use this macro as file_ and line_ will be valid and printable just not useful
    // But for this example it makes prettier output :) 
#ifdef ENKI_CUSTOM_ALLOC_FILE_AND_LINE
    printf("Allocating %g bytes with alignment %g in domain %s, total %g. File %s, line %d.\n",
        (double)size_, (double)align_, data->domainName, (double)data->totalAllocations, file_, line_ );
#else
    printf("Allocating %g bytes with alignment %g in domain %s, total %g.\n",
        (double)size_, (double)align_, data->domainName, (double)data->totalAllocations );
#endif

    return DefaultAllocFunc( align_, size_, userData_, file_, line_ );
};

void  CustomFreeFunc(  void* ptr_,    size_t size_, void* userData_, const char* file_, int line_ )
{
    CustomData* data = (CustomData*)userData_;
    data->totalAllocations -= size_;

    // we don't need to use this macro as file_ and line_ will be valid and printable just not useful
    // But for this example it makes prettier output :) 
#ifdef ENKI_CUSTOM_ALLOC_FILE_AND_LINE
    printf("Freeing %p in domain %s, total %g. File %s, line %d.\n",
        ptr_, data->domainName, (double)data->totalAllocations, file_, line_ );
#else
    (void)file_; (void)line_;
    printf("Freeing %p in domain %s, total %g.\n",
        ptr_, data->domainName, (double)data->totalAllocations );
#endif

    DefaultFreeFunc( ptr_, size_, userData_, file_, line_ );
};


int main(int argc, const char * argv[])
{
    enki::TaskSchedulerConfig config;
    config.customAllocator.alloc = CustomAllocFunc;
    config.customAllocator.free  = CustomFreeFunc;
    CustomData data{ "enkITS", 0 };
    config.customAllocator.userData = &data;

    g_TS.Initialize( config );

    ParallelTaskSet task;
    g_TS.AddTaskSetToPipe( &task );
    g_TS.WaitforTask( &task );
    g_TS.WaitforAllAndShutdown(); // ensure we shutdown before user data is destroyed.

    return 0;
}
