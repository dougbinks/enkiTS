cmake_minimum_required(VERSION 2.8)

option( ENKITS_BUILD_EXAMPLES	"Build example applications" ON )



include_directories ("${PROJECT_SOURCE_DIR}/src")

set( ENKITS_SRC
     src/Atomics.h
     src/LockLessMultiReadPipe.h
     src/Threads.h
     src/TaskScheduler.h
     src/TaskScheduler.cpp
     )
	 
add_library( enkiTS STATIC ${ENKITS_SRC} )
if(UNIX)
	find_package (Threads)
	target_link_libraries (enkiTS ${CMAKE_THREAD_LIBS_INIT})
endif()


if(ENKITS_BUILD_EXAMPLES)
	if(UNIX)
    		SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
	endif()
	if(APPLE)
    		SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
	endif()
	add_executable( Example example/Example.cpp )
	target_link_libraries(Example enkiTS )
	
endif()
