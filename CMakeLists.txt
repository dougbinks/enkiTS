cmake_minimum_required(VERSION 2.8)

project( enkiTS )

option( ENKITS_BUILD_C_INTERFACE    "Build C interface" ON )
option( ENKITS_BUILD_EXAMPLES       "Build example applications" ON )

include_directories ("${PROJECT_SOURCE_DIR}/src")

set( ENKITS_SRC
     src/Atomics.h
     src/LockLessMultiReadPipe.h
     src/Threads.h
     src/TaskScheduler.h
     src/TaskScheduler.cpp
     )
     
if( ENKITS_BUILD_C_INTERFACE )
    list( APPEND ENKITS_SRC
        src/TaskScheduler_c.h
        src/TaskScheduler_c.cpp
        )
endif()
     
add_library( enkiTS STATIC ${ENKITS_SRC} )
if(UNIX)
    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    find_package(Threads REQUIRED)
    if(CMAKE_USE_PTHREADS_INIT)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    endif()
    target_link_libraries (enkiTS ${CMAKE_THREAD_LIBS_INIT})
endif()


if(ENKITS_BUILD_EXAMPLES)
    if(UNIX)
            SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    endif()
    if(APPLE)
            SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
    add_executable( ParallelSum example/ParallelSum.cpp example/Timer.h )
    target_link_libraries(ParallelSum enkiTS )

    add_executable( PinnedTask example/PinnedTask.cpp )
    target_link_libraries(PinnedTask enkiTS )
    
    add_executable( TaskThroughput example/TaskThroughput.cpp example/Timer.h )
    target_link_libraries(TaskThroughput enkiTS )

    add_executable( TaskOverhead example/TaskOverhead.cpp example/Timer.h )
    target_link_libraries(TaskOverhead enkiTS )

if( ENKITS_BUILD_C_INTERFACE )
    add_executable( ParallelSum_c example/ParallelSum_c.c )
    target_link_libraries(ParallelSum_c enkiTS )
    
    add_executable( PinnedTask_c example/PinnedTask_c.c )
    target_link_libraries(PinnedTask_c enkiTS )
endif()    
endif()
