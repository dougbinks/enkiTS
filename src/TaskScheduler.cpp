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

#include <assert.h>

#include "TaskScheduler.h"
#include "LockLessMultiReadPipe.h"


using namespace enki;


static const uint32_t PIPESIZE_LOG2 = 8;
static const uint32_t SPIN_COUNT = 100;

#ifdef _MSC_VER
#if _MSC_VER <= 1800
#define thread_local __declspec(thread)
#endif
#endif

// each software thread gets it's own copy of gtl_threadNum, so this is safe to use as a static variable
static const uint32_t									 NO_THREAD_NUM = 0xFFFFFFFF;
static thread_local uint32_t                             gtl_threadNum = NO_THREAD_NUM;
static thread_local enki::TaskScheduler*                 gtl_pCurrTS   = NULL;


namespace enki 
{
	struct TaskSetInfo
	{
		ITaskSet*           pTask;
		TaskSetPartition    partition;
	};

	// we derive class TaskPipe rather than typedef to get forward declaration working easily
	class TaskPipe : public LockLessMultiReadPipe<PIPESIZE_LOG2,enki::TaskSetInfo> {};

	struct ThreadArgs
	{
		uint32_t		threadNum;
		TaskScheduler*  pTaskScheduler;
	};

	class ThreadNum
	{
	public:
		ThreadNum( TaskScheduler* pTaskScheduler  )
			: m_pTS( pTaskScheduler )
			, m_bNeedsRelease( false )
			, m_ThreadNum( gtl_threadNum )
			, m_PrevThreadNum( gtl_threadNum )
			, m_pPrevTS( gtl_pCurrTS )
		{
			// acquire thread id
			if( m_ThreadNum == NO_THREAD_NUM || m_pPrevTS != m_pTS )
			{

				int32_t threadcount = m_pTS->m_NumThreadsRunning++;
				if( threadcount < (int32_t)m_pTS->m_NumThreads )
				{
					int32_t index = m_pTS->m_UserThreadStackIndex++;
					assert( index < (int32_t)m_pTS->m_NumUserThreads );

					std::atomic<uint32_t>* pStackPointer = &m_pTS->m_pUserThreadNumStack[ index ];
					while(true)
					{
						m_ThreadNum = *pStackPointer;
						if( NO_THREAD_NUM != m_ThreadNum )
						{
							if(  pStackPointer->compare_exchange_strong( m_ThreadNum, NO_THREAD_NUM ) )
							{
								break;
							}
						}
					}


					gtl_threadNum = m_ThreadNum;
					gtl_pCurrTS   = m_pTS;
					m_bNeedsRelease = true;
				}
				else
				{
					--m_pTS->m_NumThreadsRunning;
				}
			}
		}

		~ThreadNum()
		{
			if( m_bNeedsRelease )
			{
				gtl_threadNum = m_PrevThreadNum;
				gtl_pCurrTS   = m_pPrevTS;
				int32_t index = --m_pTS->m_UserThreadStackIndex;
				assert( index < (int32_t)m_pTS->m_NumUserThreads );
				assert( index >= 0 );

				std::atomic<uint32_t>* pStackPointer = &m_pTS->m_pUserThreadNumStack[ index ];
				while(true)
				{
					uint32_t desired = NO_THREAD_NUM;
					if( pStackPointer->compare_exchange_strong( desired, m_ThreadNum ) )
					{
						break;
					}
				}

				--m_pTS->m_NumThreadsRunning;
			}
		}

		uint32_t		m_ThreadNum;
	private:
		ThreadNum(			  const ThreadNum& nocopy_ );
		ThreadNum& operator=( const ThreadNum& nocopy_ );

		bool			m_bNeedsRelease;
		TaskScheduler*	m_pTS;

		uint32_t		m_PrevThreadNum;
		TaskScheduler*  m_pPrevTS;
	};

}





void TaskScheduler::TaskingThreadFunction( const ThreadArgs& args_ )
{
	uint32_t threadNum				= args_.threadNum;
	TaskScheduler*  pTS				= args_.pTaskScheduler;
    gtl_threadNum					= threadNum;
	gtl_pCurrTS						= pTS;
	pTS->m_NumThreadsRunning.fetch_add(1, std::memory_order_relaxed );
    
    uint32_t spinCount = 0;
    while( pTS->m_bRunning.load( std::memory_order_relaxed ) )
    {
		if( !pTS->TryRunTask( threadNum ) )
		{
			// no tasks, will spin then wait
			++spinCount;
			if( spinCount > SPIN_COUNT )
			{
				pTS->WaitForTasks( threadNum );
			}
		}
		else
		{
			spinCount = 0;
		}
    }

    pTS->m_NumThreadsRunning.fetch_sub( 1, std::memory_order_relaxed );
	gtl_threadNum = NO_THREAD_NUM;
	gtl_pCurrTS   = NULL;
    return;
}


void TaskScheduler::StartThreads()
{
    m_bRunning = true;

    // m_NumEnkiThreads stores the number of internal threads required.
	if( m_NumEnkiThreads )
	{
		m_pThreadArgStore = new ThreadArgs[m_NumEnkiThreads];
		m_pThreads      = new std::thread*[m_NumEnkiThreads];
		for( uint32_t thread = 0; thread < m_NumEnkiThreads; ++thread )
		{
			m_pThreadArgStore[thread].threadNum      = thread;
			m_pThreadArgStore[thread].pTaskScheduler = this;
			m_pThreads[thread] = new std::thread( TaskingThreadFunction, m_pThreadArgStore[thread] );
		}
	}

    // ensure we have sufficient tasks to equally fill either all threads including main
    // or just the threads we've launched, this is outside the firstinit as we want to be able
    // to runtime change it
	if( 1 == m_NumThreads )
	{
		m_NumPartitions = 1;
	}
	else
	{
		m_NumPartitions = m_NumThreads * (m_NumThreads - 1);
	}
}

void TaskScheduler::Cleanup( bool bWait_ )
{
    // wait for them threads quit before deleting data
	if( m_bRunning )
	{
		m_bRunning = false;
		m_bUserThreadsCanRun = false;
		while( bWait_ && m_NumThreadsRunning )
		{
			// keep firing event to ensure all threads pick up state of m_bRunning
           m_NewTaskEvent.notify_all();
		}

		m_NumThreads = 0;
		m_NumEnkiThreads = 0;
		m_NumUserThreads = 0;
		delete[] m_pThreadArgStore;
		delete[] m_pThreads;
		m_pThreadArgStore = 0;
		m_pThreads = 0;
		m_NumThreadsWaiting = 0;
		m_NumThreadsRunning = 0;
		m_UserThreadStackIndex = 0;

		delete[] m_pPipesPerThread;
		m_pPipesPerThread = 0;

		delete[] m_pUserThreadNumStack;
		m_pUserThreadNumStack = 0;
	}
}

bool TaskScheduler::TryRunTask( uint32_t threadNum )
{
	// calling function should acquire a valid threadnum
	if( threadNum >= m_NumThreads )
	{
		return false;
	}

    // check for tasks
    TaskSetInfo info;
    bool bHaveTask = m_pPipesPerThread[ threadNum ].WriterTryReadFront( &info );

    if( m_NumThreads )
    {
        uint32_t checkOtherThread = 0;
        while( !bHaveTask && checkOtherThread < m_NumThreads )
        {
			if( checkOtherThread != threadNum )
			{
				bHaveTask = m_pPipesPerThread[ checkOtherThread ].ReaderTryReadBack( &info );
			}
            ++checkOtherThread;
        }
    }
        
    if( bHaveTask )
    {
        // the task has already been divided up by AddTaskSetToPipe, so just run it
        info.pTask->ExecuteRange( info.partition, threadNum );
        info.pTask->m_CompletionCount.fetch_sub(1,std::memory_order_relaxed );
    }

    return bHaveTask;
}

void TaskScheduler::WaitForTasks( uint32_t threadNum )
{
	bool bHaveTasks = false;
	for( uint32_t thread = 0; thread < m_NumThreads; ++thread )
	{
		if( !m_pPipesPerThread[ thread ].IsPipeEmpty() )
		{
			bHaveTasks = true;
			break;
		}
	}
	if( !bHaveTasks )
	{
		++m_NumThreadsWaiting;
		std::unique_lock<std::mutex> lk( m_NewTaskEventMutex );
		m_NewTaskEvent.wait( lk );
		--m_NumThreadsWaiting;
	}
}

void    TaskScheduler::AddTaskSetToPipe( ITaskSet* pTaskSet )
{
    TaskSetInfo info;
    info.pTask = pTaskSet;
    info.partition.start = 0;
    info.partition.end = pTaskSet->m_SetSize;

    // no one owns the task as yet, so just add to count
    pTaskSet->m_CompletionCount.store( 0, std::memory_order_relaxed );
	ThreadNum threadNum( this );
	if( threadNum.m_ThreadNum == NO_THREAD_NUM )
	{
		// just run in this thread
		++pTaskSet->m_CompletionCount;
        pTaskSet->ExecuteRange( info.partition, threadNum.m_ThreadNum );
        --pTaskSet->m_CompletionCount;
	}


    // divide task up and add to pipe
    uint32_t numToRun = info.pTask->m_SetSize / m_NumPartitions;
    if( numToRun == 0 ) { numToRun = 1; }
    uint32_t rangeLeft = info.partition.end - info.partition.start ;
    while( rangeLeft )
    {
        if( numToRun > rangeLeft )
        {
            numToRun = rangeLeft;
        }
        info.partition.start = pTaskSet->m_SetSize - rangeLeft;
        info.partition.end = info.partition.start + numToRun;
        rangeLeft -= numToRun;

        // add the partition to the pipe

        info.pTask->m_CompletionCount.fetch_add( 1, std::memory_order_relaxed );
        if( !m_pPipesPerThread[ gtl_threadNum ].WriterTryWriteFront( info ) )
        {
			if( m_NumThreadsWaiting.load( std::memory_order_relaxed ) > 0 )
			{
				m_NewTaskEvent.notify_all();
			}
            info.pTask->ExecuteRange( info.partition, threadNum.m_ThreadNum );
            pTaskSet->m_CompletionCount.fetch_sub(1,std::memory_order_relaxed );
        }
    }

	if( m_NumThreadsWaiting.load( std::memory_order_relaxed )  )
	{
		m_NewTaskEvent.notify_all();
	}
}

void    TaskScheduler::WaitforTaskSet( const ITaskSet* pTaskSet )
{
	ThreadNum threadNum( this );
	if( pTaskSet )
	{
		while( !pTaskSet->GetIsComplete() )
		{
			TryRunTask( threadNum.m_ThreadNum );
			// should add a spin then wait for task completion event.
		}
	}
	else
	{
			TryRunTask( threadNum.m_ThreadNum );
	}
}

void    TaskScheduler::WaitforAll()
{
	ThreadNum threadNum( this );

	int32_t amRunningThread = 0;
	if( threadNum.m_ThreadNum != NO_THREAD_NUM ) { amRunningThread = 1; }

    bool bHaveTasks = true;
    while( bHaveTasks || ( m_NumThreadsWaiting.load( std::memory_order_relaxed ) < m_NumThreadsRunning.load( std::memory_order_relaxed ) - amRunningThread ) )
    {
        TryRunTask( threadNum.m_ThreadNum );
        bHaveTasks = false;
        for( uint32_t thread = 0; thread < m_NumThreads; ++thread )
        {
            if( !m_pPipesPerThread[ thread ].IsPipeEmpty() )
            {
                bHaveTasks = true;
                break;
            }
        }
     }
}

void    TaskScheduler::WaitforAllAndShutdown()
{
    WaitforAll();
    Cleanup( true );
}

uint32_t TaskScheduler::GetNumTaskThreads() const
{
    return m_NumThreads;
}

bool	TaskScheduler::TryRunTask()
{
	ThreadNum threadNum( this );
	return TryRunTask( threadNum.m_ThreadNum );
}

void	TaskScheduler::PreUserThreadRunTasks()
{
	m_bUserThreadsCanRun = true;
}

void	TaskScheduler::UserThreadRunTasks()
{
	ThreadNum threadNum(this);

	uint32_t spinCount = 0;
    while( m_bRunning && m_bUserThreadsCanRun)
    {
		if( !TryRunTask( threadNum.m_ThreadNum ) )
		{
			// no tasks, will spin then wait
			++spinCount;
			if( spinCount > SPIN_COUNT )
			{
				WaitForTasks( threadNum.m_ThreadNum );
			}
		}
		else
		{
			spinCount = 0;
		}
   }
}

void	TaskScheduler::StopUserThreadRunTasks()
{
	m_bUserThreadsCanRun = false;
	m_NewTaskEvent.notify_all();
}

TaskScheduler::TaskScheduler()
		: m_pPipesPerThread(NULL)
		, m_NumThreads(0)
		, m_NumEnkiThreads(0)
		, m_NumUserThreads(0)
		, m_pThreadArgStore(NULL)
		, m_pUserThreadNumStack(NULL)
		, m_UserThreadStackIndex(0)
		, m_bRunning(false)
		, m_pThreads(NULL)
		, m_NumThreadsRunning(0)
		, m_NumThreadsWaiting(0)
		, m_NumPartitions(0)
		, m_bUserThreadsCanRun(false)
{
}

TaskScheduler::~TaskScheduler()
{
    Cleanup( true );
}

void    TaskScheduler::Initialize( uint32_t numThreads_ )
{
	assert( numThreads_ );

	InitializeWithUserThreads( 1, numThreads_ - 1 );
}

void   TaskScheduler::Initialize()
{
	Initialize( std::thread::hardware_concurrency() );
}

void TaskScheduler::InitializeWithUserThreads( uint32_t numUserThreads_, uint32_t numThreads_ )
{
	assert( numUserThreads_ );

	Cleanup( true ); // Stops threads, waiting for them.

	m_NumThreads	 = numThreads_ + numUserThreads_;
	m_NumEnkiThreads = numThreads_;
	m_NumUserThreads = numUserThreads_;

    m_pPipesPerThread = new TaskPipe[ m_NumThreads ];
	m_pUserThreadNumStack = new std::atomic<uint32_t>[ m_NumUserThreads ];
	for( uint32_t i = 0; i < m_NumUserThreads; ++i )
	{
		// user thread nums start at m_NumEnkiThreads
		m_pUserThreadNumStack[i] = m_NumEnkiThreads + i;
	}


    StartThreads();
}

void TaskScheduler::InitializeWithUserThreads( )
{
	InitializeWithUserThreads( std::thread::hardware_concurrency(), 0 );
}