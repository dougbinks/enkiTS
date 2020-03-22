#pragma once

#include <chrono>

// simple time class you shouldn't use outside example code.
class Timer
{
    double timeMS = 0.0;
    bool   bRunning = false;
public:
    Timer() = default;

    // start does not reset time to 0, use reset
    void Start()
    {
        bRunning = true;
        start = std::chrono::high_resolution_clock::now();
    }

    double GetTimeMS()
    {
        if( !bRunning )
        {
            return timeMS;
        }
        auto stop = std::chrono::high_resolution_clock::now();
        return 1000.0 * std::chrono::duration_cast<std::chrono::duration<double>>(stop - start).count();
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
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
};
