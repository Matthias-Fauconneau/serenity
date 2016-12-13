#pragma once
#include "IntTypes.h"
#include "tinyformat.h"
#include <chrono>

// std::chrono::high_resolution_clock has disappointing accuracy on windows
// On windows, we use the WinAPI high performance counter instead
class Timer
{
    std::chrono::time_point<std::chrono::high_resolution_clock> _start, _stop;
public:
    Timer()
    {
        start();
    }

    void start()
    {
        _start = std::chrono::high_resolution_clock::now();
    }

    void stop()
    {
        _stop = std::chrono::high_resolution_clock::now();
    }

    void bench(const std::string &s)
    {
        stop();
        std::cout << tfm::format("%s: %f s", s, elapsed()) << std::endl;
    }

    double elapsed() const
    {
        return std::chrono::duration<double>(_stop - _start).count();
    }
};
