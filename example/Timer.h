#pragma once

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#else
	#include <chrono>
#endif

// simple time struct you shouldn't use outside example code.
// made to get around precision issue with chrono on windows with VS2013
// time returned is in ms
struct Timer
{
	double timeMS;

	Timer() : timeMS(0.0) {}

	// start does not reset time to 0, use reset
	void Start()
	{
#ifdef _WIN32
		QueryPerformanceCounter( &start );
#else
		start = chrono::high_resolution_clock::now();;
#endif
	}

	void Stop()
	{
#ifdef _WIN32
		LARGE_INTEGER stop;
		QueryPerformanceCounter( &stop );
		LARGE_INTEGER elapsed;
		elapsed.QuadPart = stop.QuadPart - start.QuadPart;

		LARGE_INTEGER freq;
		QueryPerformanceFrequency( &freq );
		timeMS += (double)( 1000 * elapsed.QuadPart ) / (double)freq.QuadPart;
#else
		auto stop = chrono::high_resolution_clock::now();
		timeMS += 1000.0 * chrono::duration_cast<duration<double>>(stop - start).count();
#endif
	}

	void Reset()
	{
		timeMS = 0.0;
	}

	// GetTime() - must have called Stop() before hand.
	double GetTimeMS()
	{
		return timeMS;
	}

private:
#ifdef _WIN32
	LARGE_INTEGER start;
#else
	std::chrono::timepoint start;
#endif
};