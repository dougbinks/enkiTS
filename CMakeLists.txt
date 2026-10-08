cmake_minimum_required(VERSION 3.16)

project( enkiTS
         VERSION "1.11"
         DESCRIPTION "A C and C++ task scheduler for creating parallel programs"
         HOMEPAGE_URL "https://github.com/dougbinks/enkiTS"
         LANGUAGES C CXX)

include(GNUInstallDirs)

option( ENKITS_BUILD_C_INTERFACE    "Build C interface" ON )
option( ENKITS_BUILD_EXAMPLES       "Build example applications" ON )
option( ENKITS_BUILD_SHARED         "Build shared library" OFF )
option( ENKITS_INSTALL              "Generate installation target" OFF )
option( ENKITS_SANITIZE             "Build with sanitizers" OFF)

set( ENKITS_TASK_PRIORITIES_NUM "3" CACHE STRING "Number of task priorities, 1-5, 0 for defined by defaults in source" )

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

if(ENKITS_SANITIZE)
	if(MSVC)
		add_compile_options(/fsanitize=address)
        add_link_options(/INCREMENTAL:NO)
	else()
		# add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
		add_compile_options(-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=undefined)
		add_link_options(-fsanitize=address -fsanitize-address-use-after-scope -fsanitize=undefined)
	endif()
endif()

if( ENKITS_BUILD_SHARED )
    add_library( enkiTS SHARED ${ENKITS_SRC} )
    target_compile_definitions( enkiTS PRIVATE ENKITS_BUILD_DLL=1 )
    target_compile_definitions( enkiTS INTERFACE ENKITS_DLL=1 )
    set_target_properties( enkiTS PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
    if( UNIX )
        if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
             SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
        endif()
    endif ()
else()
    add_library( enkiTS STATIC ${ENKITS_SRC} )
endif()

target_include_directories( enkiTS PUBLIC
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/enkiTS> )

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
    install(
        TARGETS enkiTS
        EXPORT enkiTSConfig
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES ${ENKITS_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/enkiTS)
    install(
        EXPORT enkiTSConfig
        NAMESPACE enkiTS::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/enkiTS)
endif()

target_compile_features( enkiTS PUBLIC cxx_std_11 )

if( ENKITS_BUILD_EXAMPLES )
    add_subdirectory( example )
endif()
