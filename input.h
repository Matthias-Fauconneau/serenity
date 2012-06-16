#pragma once
#include "process.h"
#include "signal.h"
#include "map.h"

//#include <linux/input.h>
enum { KEY_POWER=116, BTN_EXTRA=0x114 };

/// \a Input poll a linux evdev interface for any events
struct Input : Poll {
    int fd;
    map<int, signal<> > keyPress;
    Input(const char* path);
    ~Input();
    void event(pollfd p);
};
