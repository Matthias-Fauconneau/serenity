#pragma once
#include "process.h"

/// Returns relative time in milliseconds
int getRealTime();

/// Returns Unix real-time in seconds
int getUnixTime();

/// Returns current date formatted using \a format string
string date(string&& format);

struct Timer : Poll {
    int fd;
    Timer();
    void setAbsolute(int date);
    virtual void expired() =0;
    void event(pollfd);
};
