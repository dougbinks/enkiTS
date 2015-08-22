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

#pragma once

#include <atomic>
#include <thread>
#include <condition_variable>
#include <stdint.h>
#include <functional>

namespace enki
{

	struct TaskSetPartition
	{
		uint32_t start;
		uint32_t end;
	};

	class  TaskScheduler;
	class  TaskPipe;
	struct ThreadArgs;
	class  ThreadNum;


	// Subclass ITaskSet to create tasks.
	// TaskSets can be re-used, but check
	class ITaskSet
	{
	public:
        ITaskSet()
            : m_SetSize(1)
            , m_RunningCount(0)
        {}

        ITaskSet( uint32_t setSize_ )
            : m_SetSize( setSize_ )
            , m_RunningCount(0)
        {}
		// Execute range should be overloaded to process tasks. It will be called with a
		// range_ where range.start >= 0; range.start < range.end; and range.end < m_SetSize;
		// The range values should be mapped so that linearly processing them in order is cache friendly
		// i.e. neighbouring values should be close together.
		// threadnum should not be used for changing processing of data, it's intended purpose
		// is to allow per-thread data buckets for output.
		// threadnum < TaskScheduler::GetNumTaskThreads()
		virtual void            ExecuteRange( TaskSetPartition range, uint32_t threadnum  ) = 0;

		// Size of set - usually the number of data items to be processed, see ExecuteRange. Defaults to 1
		uint32_t                m_SetSize;

		bool                    GetIsComplete() const
		{
			return 0 == m_RunningCount.load( std::memory_order_relaxed );
		}
	private:
		friend class           TaskScheduler;
		std::atomic<int32_t>   m_RunningCount;
	};

	// A utility task set for creating tasks based on std::func.
	typedef std::function<void (TaskSetPartition range, uint32_t threadnum  )> TaskSetFunction;
	class TaskSet : public ITaskSet
	{
	public:
		TaskSet() = default;
		TaskSet( TaskSetFunction func_ ) : m_Function( func_ ) {}
		TaskSet( uint32_t setSize_, TaskSetFunction func_ ) : ITaskSet( setSize_ ), m_Function( func_ ) {}


		virtual void            ExecuteRange( TaskSetPartition range, uint32_t threadnum  )
		{
			m_Function( range, threadnum );
		}

		TaskSetFunction m_Function;
	};


	class TaskScheduler
	{
	public:
		// TaskScheduler is not a singleton, but usual case is to have only one.
		// It is safe to have multiple task schedulers.
		TaskScheduler();
		~TaskScheduler();

		// Before adding tasks call one of:
		//  Initialize(),
		//  Initialize( numThreads_ ),
		//  InitializeWithUserThreads(),
		//  InitializeWithUserThreads( numUserThreads_, numThreads_ )

		// Initialize() will create GetNumHardwareThreads()-1 threads, which is
		// sufficient to fill the system when including the main thread.
		// Initialize can be called multiple times - it will wait for completion
		// before re-initializing.
		// Equivalent to Initialize( GetNumHardwareThreads() );
		void			Initialize();

		// Initialize( numThreads_ ).
		// numThreads_ must be > 0.
		// Will create numThreads_-1 task threads, as one thread is
		// the thread on which the initialize was called.
		// Equivalent to InitializeWithUserThreads( 1, numThreads_ - 1 );
		void			Initialize( uint32_t numThreads_ );


		// Initialize( numUserThreads_, numThreads_ ).
		// This version is intended for use with other task systems
		// or user created threads.
		// Will create sufficient space in internal structures
		// for GetNumHardwareThreads() user task functions.
		// This version will create no EnkiTS held threads of it's own.
		// Equivalent to InitializeWithUserThreads( GetNumHardwareThreads(), 0 );
		void			InitializeWithUserThreads();

		// Initialize( numUserThreads_, numThreads_ ).
		// numUserThreads_ must be > 0.
		// This version is intended for use with other task systems
		// or user created threads.
		// Will create numThreads_-1 task threads, as thread 0 is
		// the thread on which the initialize was called.
		// Additionally, will create internal structures sufficient to run
		// numUserThreads_ task functions.
		void			InitializeWithUserThreads( uint32_t numUserThreads_, uint32_t numThreads_ );


		// Adds the TaskSet to pipe and returns if the pipe is not full.
		// If the pipe is full, pTaskSet is run.
		// should only be called from main thread, or within a task
		void            AddTaskSetToPipe( ITaskSet* pTaskSet );

		// Runs the TaskSets in pipe until true == pTaskSet->GetIsComplete();
		// This is safe to call from any thread.
		// If called with 0 it will try to run tasks, and return if none available.
		void            WaitforTaskSet( const ITaskSet* pTaskSet );

		// Waits for all task sets to complete - not guaranteed to work unless we know we
		// are in a situation where tasks aren't being continuosly added.
		void            WaitforAll();

		// Waits for all task sets to complete and shutdown threads - not guaranteed to work unless we know we
		// are in a situation where tasks aren't being continuosly added.
		void            WaitforAllAndShutdown();

		// Returns the total maximum number of threads which could be used for running tasks.
		// This includes both user task threads and enkiTS internally created threads.
		// Useful for creating thread-safe tables as task function parameter threadnum
		// is guaranteed to be < GetNumTaskThreads()
		uint32_t        GetNumTaskThreads() const;

		// TryRunTask will try to run a single task from the pipe.
		// Returns true if it ran a task, false if not.
		// Safe to run on any thread.
		bool			TryRunTask();

		// PreUserThreadRunTasks sets an internal state which controls
		// the lifetime of UserThreadRunTasks().
		// Calling this sets UserThreadRunTasks() to run continuously
		// until  StopUserThreadRunTasks() is called.
		void			PreUserThreadRunTasks();

		// Runs tasks. Lifetime controlled by PreUserThreadRunTasks() and
		// StopUserThreadRunTasks(). Will exit immediatly if
		// PreUserThreadRunTasks() has not been called.
		void			UserThreadRunTasks();

		// StopUserThreadRunTasks sets an internal state which controls
		// the lifetime of UserThreadRunTasks().
		// Calling this sets UserThreadRunTasks() to exit as soon
		// as it has finished the current task.
		// If you want all tasks to complete, call WaitforAll()
		// before calling this function.
		// StopUserThreadRunTasks() will return immediatly,
		// it will not wait for all UserThreadRunTasks() functions
		// to exit.
		void			StopUserThreadRunTasks();

	private:
		friend class ThreadNum;

		static void		 TaskingThreadFunction( const ThreadArgs& args_ );
		template<bool ISUSERTASK>
		void             WaitForTasks( uint32_t threadNum );
		bool             TryRunTask( uint32_t threadNum, uint32_t& hintPipeToCheck_io_ );
		void             StartThreads();
		void             Cleanup( bool bWait_ );


		TaskPipe*                                                m_pPipesPerThread;

		uint32_t                                                 m_NumThreads;
		uint32_t												 m_NumEnkiThreads;
		uint32_t												 m_NumUserThreads;
		ThreadArgs*                                              m_pThreadArgStore;
		std::atomic<bool>										 m_bUserThreadsCanRun;
		std::thread**											 m_pThreads;
		std::atomic<int32_t>									 m_UserThreadStackIndex;
		std::atomic<uint32_t>*									 m_pUserThreadNumStack;
		std::atomic<bool>										 m_bRunning;
		std::atomic<int32_t>                                     m_NumThreadsRunning;
		std::atomic<int32_t>                                     m_NumThreadsWaiting;
		uint32_t                                                 m_NumPartitions;
		std::condition_variable                                  m_NewTaskEvent;
		std::mutex												 m_NewTaskEventMutex;


		TaskScheduler( const TaskScheduler& nocopy );
		TaskScheduler& operator=( const TaskScheduler& nocopy );
	};

}