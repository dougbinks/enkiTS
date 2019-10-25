cmake_minimum_required(VERSION 3.0)

project( enkiTS )

option( ENKITS_BUILD_C_INTERFACE    "Build C interface" ON )
option( ENKITS_BUILD_EXAMPLES       "Build example applications" ON )
option( ENKITS_BUILD_SHARED         "Build shared library" OFF )
option( ENKITS_INSTALL              "Generate installation target" OFF )

set( ENKITS_TASK_PRIORITIES_NUM "3" CACHE STRING "Number of task priorities, 1-5, 0 for defined by defaults in source" ) 

include_directories( "${PROJECT_SOURCE_DIR}/src" )

set( ENKITS_HEADERS
    src/LockLessMultiReadPipe.h
    src/TaskScheduler.h
    )

set( ENKITS_SRC
     src/TaskScheduler.cpp
     )

if( ENKITS_BUILD_C_INTERFACE )
    list( APPEND ENKITS_HEADERS
        src/TaskScheduler_c.h
        )
		
    list( APPEND ENKITS_SRC
        src/TaskScheduler_c.cpp
        )
endif()

list( APPEND ENKITS_SRC ${ENKITS_HEADERS} )

if( ENKITS_BUILD_SHARED )
    add_library( enkiTS SHARED ${ENKITS_SRC} )
    target_compile_definitions( enkiTS PRIVATE ENKITS_BUILD_DLL=1 )
    target_compile_definitions( enkiTS INTERFACE ENKITS_DLL=1 )
    if( UNIX )
	    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang") 
	         SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    	endif()
	endif ()
else()
    add_library( enkiTS STATIC ${ENKITS_SRC} )
endif()


if( ENKITS_TASK_PRIORITIES_NUM GREATER "0" )
    target_compile_definitions( enkiTS PUBLIC "ENKITS_TASK_PRIORITIES_NUM=${ENKITS_TASK_PRIORITIES_NUM}" )    
endif()

if( UNIX )
    set( CMAKE_THREAD_PREFER_PTHREAD TRUE )
    find_package( Threads REQUIRED )
    if( CMAKE_USE_PTHREADS_INIT )
        set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread" )
    endif()
    target_link_libraries( enkiTS ${CMAKE_THREAD_LIBS_INIT} )
endif()

if( ENKITS_INSTALL )
    install(TARGETS enkiTS DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/enkiTS")
    install(FILES ${ENKITS_HEADERS} DESTINATION "${CMAKE_INSTALL_PREFIX}/include/enkiTS")
endif()

if( UNIX )
    SET( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11" )
endif()
if( APPLE )
    SET( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++" )
endif()

if( ENKITS_BUILD_EXAMPLES )
    add_executable( ParallelSum example/ParallelSum.cpp example/Timer.h )
    target_link_libraries(ParallelSum enkiTS )

    add_executable( PinnedTask example/PinnedTask.cpp )
    target_link_libraries(PinnedTask enkiTS )

    add_executable( LambdaTask example/LambdaTask.cpp example/Timer.h )
    target_link_libraries(LambdaTask enkiTS )

    if( ENKITS_TASK_PRIORITIES_NUM GREATER "0" )
        add_executable( Priorities example/Priorities.cpp )
        target_link_libraries(Priorities enkiTS )
    endif()
    
    add_executable( TaskThroughput example/TaskThroughput.cpp example/Timer.h )
    target_link_libraries(TaskThroughput enkiTS )

    add_executable( TaskOverhead example/TaskOverhead.cpp example/Timer.h )
    target_link_libraries(TaskOverhead enkiTS )

    add_executable( TestWaitforTask example/TestWaitforTask.cpp )
    target_link_libraries(TestWaitforTask enkiTS )

    add_executable( ExternalTaskThread example/ExternalTaskThread.cpp )
    target_link_libraries(ExternalTaskThread enkiTS )

if( ENKITS_BUILD_C_INTERFACE )
    add_executable( ParallelSum_c example/ParallelSum_c.c )
    target_link_libraries(ParallelSum_c enkiTS )
    
    add_executable( PinnedTask_c example/PinnedTask_c.c )
    target_link_libraries(PinnedTask_c enkiTS )

    if( ENKITS_TASK_PRIORITIES_NUM GREATER "0" )
        add_executable( Priorities_c example/Priorities_c.c )
        target_link_libraries(Priorities_c enkiTS )
    endif()

    add_executable( ExternalTaskThread_c example/ExternalTaskThread_c.c )
    target_link_libraries(ExternalTaskThread_c enkiTS )

endif()    
endif()
