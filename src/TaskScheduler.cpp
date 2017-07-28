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


static const uint32_t PIPESIZE_LOG2              = 8;
static const uint32_t SPIN_COUNT                 = 100;
static const uint32_t SPIN_BACKOFF_MULTIPLIER    = 10;
static const uint32_t MAX_NUM_INITIAL_PARTITIONS = 8;

// each software thread gets it's own copy of gtl_threadNum, so this is safe to use as a static variable
static const uint32_t									 NO_THREAD_NUM = 0xFFFFFFFF;
static THREAD_LOCAL uint32_t                             gtl_threadNum = NO_THREAD_NUM;
static THREAD_LOCAL enki::TaskScheduler*                 gtl_pCurrTS   = NULL;

namespace enki 
{
	struct SubTaskSet
	{
		ITaskSet*           pTask;
		TaskSetPartition    partition;
	};

	// we derive class TaskPipe rather than typedef to get forward declaration working easily
	class TaskPipe : public LockLessMultiReadPipe<PIPESIZE_LOG2,enki::SubTaskSet> {};

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
				int32_t threadcount = AtomicAdd( &m_pTS->m_NumThreadsRunning, 1 );
				if( threadcount < (int32_t)m_pTS->m_NumThreads )
				{
					int32_t index = AtomicAdd( &m_pTS->m_UserThreadStackIndex, 1 );
					assert( index < (int32_t)m_pTS->m_NumUserThreads );

					volatile uint32_t* pStackPointer = &m_pTS->m_pUserThreadNumStack[ index ];
					while(true)
					{
						m_ThreadNum = *pStackPointer;
						if( NO_THREAD_NUM != m_ThreadNum )
						{
							uint32_t old = AtomicCompareAndSwap( pStackPointer, NO_THREAD_NUM, m_ThreadNum );
							if( old == m_ThreadNum )
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
					AtomicAdd( &m_pTS->m_NumThreadsRunning, -1 );
				}
			}
		}

		~ThreadNum()
		{
			if( m_bNeedsRelease )
			{
				gtl_threadNum = m_PrevThreadNum;
				gtl_pCurrTS   = m_pPrevTS;
				int32_t index = AtomicAdd( &m_pTS->m_UserThreadStackIndex, -1 ) - 1;
				assert( index < (int32_t)m_pTS->m_NumUserThreads );
				assert( index >= 0 );

				volatile uint32_t* pStackPointer = &m_pTS->m_pUserThreadNumStack[ index ];
				while(true)
				{
					uint32_t old = AtomicCompareAndSwap( pStackPointer, m_ThreadNum, NO_THREAD_NUM );
					if( old == NO_THREAD_NUM )
					{
						break;
					}
				}

				AtomicAdd( &m_pTS->m_NumThreadsRunning, -1 );
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

namespace
{
	SubTaskSet       SplitTask( SubTaskSet& subTask_, uint32_t rangeToSplit_ )
	{
		SubTaskSet splitTask = subTask_;
		uint32_t rangeLeft = subTask_.partition.end - subTask_.partition.start;

        if( rangeToSplit_ > rangeLeft )
        {
            rangeToSplit_ = rangeLeft;
        }
        splitTask.partition.end = subTask_.partition.start + rangeToSplit_;
		subTask_.partition.start = splitTask.partition.end;
		return splitTask;
	}

	inline void Pause()
	{
	#if defined _WIN32 && defined _M_X86
		_mm_pause();
	#elif defined __i386__
		asm("pause");
	#else
	#endif
	}
}


static void SafeCallback(ProfilerCallbackFunc func_, uint32_t threadnum_)
{
	if( func_ )
	{
		func_(threadnum_);
	}
}

ProfilerCallbacks* TaskScheduler::GetProfilerCallbacks()
{
	return &m_ProfilerCallbacks;
}


THREADFUNC_DECL TaskScheduler::TaskingThreadFunction( void* pArgs )
{
	ThreadArgs args					= *(ThreadArgs*)pArgs;
	uint32_t threadNum				= args.threadNum;
	TaskScheduler*  pTS				= args.pTaskScheduler;
    gtl_threadNum					= threadNum;
	gtl_pCurrTS						= pTS;

    AtomicAdd( &pTS->m_NumThreadsRunning, 1 );

    SafeCallback( pTS->m_ProfilerCallbacks.threadStart, threadNum );

	uint32_t spinCount = 0;
	uint32_t hintPipeToCheck_io = threadNum + 1;	// does not need to be clamped.
    while( pTS->m_bRunning )
    {
		if( !pTS->TryRunTask( threadNum, hintPipeToCheck_io ) )
		{
			// no tasks, will spin then wait
			++spinCount;
			if( spinCount > SPIN_COUNT )
			{
				pTS->WaitForTasks( threadNum );
            }
			else
			{
				uint32_t spinBackoffCount = spinCount * SPIN_BACKOFF_MULTIPLIER;
				while( spinBackoffCount )
				{
					Pause();
					--spinBackoffCount;
				}
			}
		}
		else
		{
			spinCount = 0;
		}
    }
	gtl_threadNum = NO_THREAD_NUM;

    AtomicAdd( &pTS->m_NumThreadsRunning, -1 );
	SafeCallback( pTS->m_ProfilerCallbacks.threadStop, threadNum );

    return 0;
}


void TaskScheduler::StartThreads()
{
    m_bRunning = true;

    m_NewTaskEvent = EventCreate();

    // m_NumEnkiThreads stores the number of internal threads required.
	if( m_NumEnkiThreads )
	{
		m_pThreadArgStore = new ThreadArgs[m_NumEnkiThreads];
		m_pThreadIDs      = new threadid_t[m_NumEnkiThreads];
		for( uint32_t thread = 0; thread < m_NumEnkiThreads; ++thread )
		{
			m_pThreadArgStore[thread].threadNum      = thread;
			m_pThreadArgStore[thread].pTaskScheduler = this;
			ThreadCreate( &m_pThreadIDs[thread], TaskingThreadFunction, &m_pThreadArgStore[thread] );
		}
	}

    // ensure we have sufficient tasks to equally fill either all threads including main
    // or just the threads we've launched, this is outside the firstinit as we want to be able
    // to runtime change it
	if( 1 == m_NumThreads )
	{
		m_NumPartitions = 1;
		m_NumInitialPartitions = 1;
	}
	else
	{
		m_NumPartitions = m_NumThreads * (m_NumThreads - 1);
		m_NumInitialPartitions = m_NumThreads - 1;
		if( m_NumInitialPartitions > MAX_NUM_INITIAL_PARTITIONS )
		{
			m_NumInitialPartitions = MAX_NUM_INITIAL_PARTITIONS;
		}
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
			EventSignal( m_NewTaskEvent );
		}

		for( uint32_t thread = 0; thread < m_NumEnkiThreads; ++thread )
		{
			ThreadTerminate( m_pThreadIDs[thread] );
		}

		m_NumThreads = 0;
		m_NumEnkiThreads = 0;
		m_NumUserThreads = 0;
		delete[] m_pThreadArgStore;
		delete[] m_pThreadIDs;
		m_pThreadArgStore = 0;
		m_pThreadIDs = 0;
		EventClose( m_NewTaskEvent );

		m_NumThreadsWaiting = 0;
		m_NumThreadsRunning = 0;
		m_UserThreadStackIndex = 0;

		delete[] m_pPipesPerThread;
		m_pPipesPerThread = 0;

		delete[] m_pUserThreadNumStack;
		m_pUserThreadNumStack = 0;
	}
}

bool TaskScheduler::TryRunTask( uint32_t threadNum, uint32_t& hintPipeToCheck_io_ )
{
	// calling function should acquire a valid threadnum
	if( threadNum >= m_NumThreads )
	{
		return false;
	}

    // check for tasks
    SubTaskSet subTask;
    bool bHaveTask = m_pPipesPerThread[ threadNum ].WriterTryReadFront( &subTask );

	uint32_t threadToCheck = hintPipeToCheck_io_;
	uint32_t checkCount = 0;
    while( !bHaveTask && checkCount < m_NumThreads )
    {
		threadToCheck = ( hintPipeToCheck_io_ + checkCount ) % m_NumThreads;
		if( threadToCheck != threadNum )
		{
			bHaveTask = m_pPipesPerThread[ threadToCheck ].ReaderTryReadBack( &subTask );
		}
		++checkCount;
    }
        
    if( bHaveTask )
    {
		// update hint, will preserve value unless actually got task from another thread.
		hintPipeToCheck_io_ = threadToCheck;

		uint32_t partitionSize = subTask.partition.end - subTask.partition.start;
		if( subTask.pTask->m_RangeToRun < partitionSize )
		{
			SubTaskSet taskToRun = SplitTask( subTask, subTask.pTask->m_RangeToRun );
			SplitAndAddTask( gtl_threadNum, subTask, subTask.pTask->m_RangeToRun, 0 );
			taskToRun.pTask->ExecuteRange( taskToRun.partition, threadNum );
			AtomicAdd( &taskToRun.pTask->m_RunningCount, -1 );
		}
		else
		{

			// the task has already been divided up by AddTaskSetToPipe, so just run it
			subTask.pTask->ExecuteRange( subTask.partition, threadNum );
			AtomicAdd( &subTask.pTask->m_RunningCount, -1 );
		}
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
        SafeCallback( m_ProfilerCallbacks.waitStart, threadNum );
        AtomicAdd( &m_NumThreadsWaiting, 1 );
		EventWait( m_NewTaskEvent, EVENTWAIT_INFINITE );
		AtomicAdd( &m_NumThreadsWaiting, -1 );
        SafeCallback( m_ProfilerCallbacks.waitStop, threadNum );
    }
}

void TaskScheduler::WakeThreads()
{
    if( m_NumThreadsWaiting > 0 )
	{
		EventSignal( m_NewTaskEvent );
	}
}


void TaskScheduler::SplitAndAddTask( uint32_t threadNum_, SubTaskSet subTask_,
	uint32_t rangeToSplit_, int32_t runningCountOffset_ )
{
    int32_t numAdded = 0;
    while( subTask_.partition.start != subTask_.partition.end )
    {
        SubTaskSet taskToAdd = SplitTask( subTask_, rangeToSplit_ );

        // add the partition to the pipe
        ++numAdded;
        if( !m_pPipesPerThread[ gtl_threadNum ].WriterTryWriteFront( taskToAdd ) )
        {
			if( numAdded > 1 )
			{
				WakeThreads();
			}
			// alter range to run the appropriate fraction
			if( taskToAdd.pTask->m_RangeToRun < rangeToSplit_ )
			{
				taskToAdd.partition.end = taskToAdd.partition.start + taskToAdd.pTask->m_RangeToRun;
				subTask_.partition.start = taskToAdd.partition.end;
			}
            taskToAdd.pTask->ExecuteRange( taskToAdd.partition, threadNum_ );
            --numAdded;
        }
    }

    // increment running count by number added
    AtomicAdd( &subTask_.pTask->m_RunningCount, numAdded + runningCountOffset_ );

	WakeThreads();
}

void    TaskScheduler::AddTaskSetToPipe( ITaskSet* pTaskSet )
{
	// set running count to -1 to guarantee it won't be found complete until all subtasks added
    pTaskSet->m_RunningCount = -1;

    // divide task up and add to pipe
    pTaskSet->m_RangeToRun = pTaskSet->m_SetSize / m_NumPartitions;
    if( pTaskSet->m_RangeToRun < pTaskSet->m_MinRange ) { pTaskSet->m_RangeToRun = pTaskSet->m_MinRange; }

	uint32_t rangeToSplit = pTaskSet->m_SetSize / m_NumInitialPartitions;
	if( rangeToSplit < pTaskSet->m_MinRange ) { rangeToSplit = pTaskSet->m_MinRange; }

    SubTaskSet subTask;
    subTask.pTask = pTaskSet;
    subTask.partition.start = 0;
    subTask.partition.end = pTaskSet->m_SetSize;

	ThreadNum threadNum( this );
	if( threadNum.m_ThreadNum == NO_THREAD_NUM )
	{
		// just run in this thread
        pTaskSet->ExecuteRange( subTask.partition, threadNum.m_ThreadNum );
        pTaskSet->m_RunningCount = 0;
		return;
	}

	SplitAndAddTask( gtl_threadNum, subTask, rangeToSplit, 1 );
}

void    TaskScheduler::WaitforTaskSet( const ITaskSet* pTaskSet )
{
	ThreadNum threadNum( this );
	uint32_t hintPipeToCheck_io = threadNum.m_ThreadNum  + 1;	// does not need to be clamped.
	if( pTaskSet )
	{
		while( pTaskSet->m_RunningCount )
		{
			TryRunTask( threadNum.m_ThreadNum, hintPipeToCheck_io );
			// should add a spin then wait for task completion event.
		}
	}
	else
	{
			TryRunTask( threadNum.m_ThreadNum, hintPipeToCheck_io );
	}
}

void    TaskScheduler::WaitforAll()
{
	ThreadNum threadNum( this );

	int32_t amRunningThread = 0;
	if( threadNum.m_ThreadNum != NO_THREAD_NUM ) { amRunningThread = 1; }

    bool bHaveTasks = true;
 	uint32_t hintPipeToCheck_io = threadNum.m_ThreadNum  + 1;	// does not need to be clamped.
    while( bHaveTasks || ( m_NumThreadsWaiting < m_NumThreadsRunning - amRunningThread ) )
    {
        TryRunTask( threadNum.m_ThreadNum, hintPipeToCheck_io );
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
	uint32_t hintPipeToCheck_io = threadNum.m_ThreadNum  + 1;	// does not need to be clamped.
	return TryRunTask( threadNum.m_ThreadNum, hintPipeToCheck_io );
}

void	TaskScheduler::PreUserThreadRunTasks()
{
	m_bUserThreadsCanRun = true;
}

void	TaskScheduler::UserThreadRunTasks()
{
	ThreadNum threadNum(this);

	uint32_t spinCount = 0;
	uint32_t hintPipeToCheck_io = threadNum.m_ThreadNum  + 1;	// does not need to be clamped.
    while( m_bRunning && m_bUserThreadsCanRun)
    {
		if( !TryRunTask( threadNum.m_ThreadNum, hintPipeToCheck_io ) )
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
	ThreadNum threadNum(this);

	int32_t amUserThread    = 0;
	int32_t amRunningThread = 0;
	if( threadNum.m_ThreadNum != NO_THREAD_NUM ) {
		amRunningThread = 1;
		if( threadNum.m_ThreadNum >= m_NumEnkiThreads ) { 
			amUserThread = 1;
		}
	}

	do
	{
		EventSignal( m_NewTaskEvent );	// wake up any sleeping threads.
	} while( m_NumThreadsWaiting && ( m_UserThreadStackIndex > amUserThread ) );
}

TaskScheduler::TaskScheduler()
		: m_pPipesPerThread(NULL)
		, m_NumThreads(0)
		, m_NumEnkiThreads(0)
		, m_NumUserThreads(0)
		, m_pThreadArgStore(NULL)
		, m_pThreadIDs(NULL)
		, m_pUserThreadNumStack(NULL)
		, m_UserThreadStackIndex(0)
		, m_bRunning(false)
		, m_NumThreadsRunning(0)
		, m_NumThreadsWaiting(0)
		, m_NumPartitions(0)
		, m_bUserThreadsCanRun(false)
{
	memset(&m_ProfilerCallbacks, 0, sizeof(m_ProfilerCallbacks));
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
	Initialize( GetNumHardwareThreads() );
}

void TaskScheduler::InitializeWithUserThreads( uint32_t numUserThreads_, uint32_t numThreads_ )
{
	assert( numUserThreads_ );

	Cleanup( true ); // Stops threads, waiting for them.

	m_NumThreads	 = numThreads_ + numUserThreads_;
	m_NumEnkiThreads = numThreads_;
	m_NumUserThreads = numUserThreads_;

    m_pPipesPerThread = new TaskPipe[ m_NumThreads ];
	m_pUserThreadNumStack = new uint32_t[ m_NumUserThreads ];
	for( uint32_t i = 0; i < m_NumUserThreads; ++i )
	{
		// user thread nums start at m_NumEnkiThreads
		m_pUserThreadNumStack[i] = m_NumEnkiThreads + i;
	}


    StartThreads();
}

void TaskScheduler::InitializeWithUserThreads( )
{
	InitializeWithUserThreads( GetNumHardwareThreads(), 0 );
}