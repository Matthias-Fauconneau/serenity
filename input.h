#pragma once
#include "process.h"
#include "signal.h"

//Key enums
#include <linux/input.h>

/// \a Input poll a linux evdev interface for any events
struct Input : Poll {
    int fd;
    map<int, signal<> > keyPress;
    Input(const char* path);
    ~Input();
    void event(pollfd p);
};
