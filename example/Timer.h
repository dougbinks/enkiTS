#pragma once

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
#else
    #include <chrono>
#endif

// simple time class you shouldn't use outside example code.
// made to get around precision issue with chrono on windows with VS2013
// time returned is in ms
class Timer
{
    double timeMS;
    bool   bRunning;
public:
    Timer() : timeMS(0.0), bRunning(false) {}

    // start does not reset time to 0, use reset
    void Start()
    {
        bRunning = true;
#ifdef _WIN32
        QueryPerformanceCounter( &start );
#else
        start = std::chrono::high_resolution_clock::now();
#endif
    }

    double GetTimeMS()
    {
        if( !bRunning )
        {
            return timeMS;
        }
#ifdef _WIN32
        LARGE_INTEGER stop;
        QueryPerformanceCounter( &stop );
        LARGE_INTEGER elapsed;
        elapsed.QuadPart = stop.QuadPart - start.QuadPart;

        LARGE_INTEGER freq;
        QueryPerformanceFrequency( &freq );
        return (double)( 1000 * elapsed.QuadPart ) / (double)freq.QuadPart;
#else
        auto stop = std::chrono::high_resolution_clock::now();
        return 1000.0 * std::chrono::duration_cast<std::chrono::duration<double>>(stop - start).count();
#endif
    };

    void Stop()
    {
        if( bRunning )
        {
            timeMS += GetTimeMS();
            bRunning = false;
        }
    }

    void Reset()
    {
        timeMS = 0.0;
        bRunning = false;
    }

private:
#ifdef _WIN32
    LARGE_INTEGER start;
#else
    std::chrono::high_resolution_clock::time_point start;
#endif
};