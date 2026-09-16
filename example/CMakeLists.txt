cmake_minimum_required(VERSION 3.16)
project(enkiTS_Examples LANGUAGES C CXX)

set(ENKITS_TARGET enkiTS)
if (PROJECT_IS_TOP_LEVEL)
    find_package(enkiTS CONFIG REQUIRED)
    set(ENKITS_TARGET enkiTS::enkiTS)
endif()

add_executable( ParallelSum ParallelSum.cpp Timer.h )
target_link_libraries(ParallelSum ${ENKITS_TARGET} )

add_executable( PinnedTask PinnedTask.cpp )
target_link_libraries(PinnedTask ${ENKITS_TARGET} )

add_executable( LambdaTask LambdaTask.cpp Timer.h )
target_link_libraries(LambdaTask ${ENKITS_TARGET} )

if( ENKITS_TASK_PRIORITIES_NUM GREATER "0" )
    add_executable( Priorities Priorities.cpp )
target_link_libraries(Priorities ${ENKITS_TARGET} )
endif()

add_executable( TaskThroughput TaskThroughput.cpp Timer.h )
target_link_libraries(TaskThroughput ${ENKITS_TARGET} )

add_executable( TaskOverhead TaskOverhead.cpp Timer.h )
target_link_libraries(TaskOverhead ${ENKITS_TARGET} )

add_executable( TestWaitforTask TestWaitforTask.cpp )
target_link_libraries(TestWaitforTask ${ENKITS_TARGET} )

add_executable( ExternalTaskThread ExternalTaskThread.cpp )
target_link_libraries(ExternalTaskThread ${ENKITS_TARGET} )

add_executable( CustomAllocator CustomAllocator.cpp )
target_link_libraries(CustomAllocator ${ENKITS_TARGET} )

add_executable( TestAll TestAll.cpp )
target_link_libraries(TestAll ${ENKITS_TARGET} )

add_executable( Dependencies Dependencies.cpp )
target_link_libraries(Dependencies ${ENKITS_TARGET} )

add_executable( CompletionAction CompletionAction.cpp )
target_link_libraries(CompletionAction ${ENKITS_TARGET} )

add_executable( WaitForNewPinnedTasks WaitForNewPinnedTasks.cpp )
target_link_libraries(WaitForNewPinnedTasks ${ENKITS_TARGET} )

if( ENKITS_BUILD_C_INTERFACE )
    add_executable( ParallelSum_c ParallelSum_c.c )
target_link_libraries(ParallelSum_c ${ENKITS_TARGET} )

    add_executable( PinnedTask_c PinnedTask_c.c )
target_link_libraries(PinnedTask_c ${ENKITS_TARGET} )

    if( ENKITS_TASK_PRIORITIES_NUM GREATER "0" )
        add_executable( Priorities_c Priorities_c.c )
target_link_libraries(Priorities_c ${ENKITS_TARGET} )
    endif()

    add_executable( ExternalTaskThread_c ExternalTaskThread_c.c )
target_link_libraries(ExternalTaskThread_c ${ENKITS_TARGET} )

    add_executable( CustomAllocator_c CustomAllocator_c.c )
target_link_libraries(CustomAllocator_c ${ENKITS_TARGET} )

    add_executable( Dependencies_c Dependencies_c.c )
target_link_libraries(Dependencies_c ${ENKITS_TARGET} )

    add_executable( CompletionAction_c CompletionAction_c.c )
target_link_libraries(CompletionAction_c ${ENKITS_TARGET} )

    add_executable( WaitForNewPinnedTasks_c WaitForNewPinnedTasks_c.c )
target_link_libraries(WaitForNewPinnedTasks_c ${ENKITS_TARGET} )
endif()