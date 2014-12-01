#pragma once
#include "thread.h"
#include "function.h"

struct VideoInput : Device, Poll {
    function<void(ref<byte>, uint64)> write;
    uint width, height, frameRate = 0;
    array<Map> buffers;
    size_t videoTime = 0;
    uint64 lastTimeStamp = 0;

    VideoInput(function<void(ref<byte>, uint64)> write, Thread& thread=mainThread) : Device("/dev/video0"), Poll(Device::fd,POLLIN,thread), write(write) { setup(); }
    void setup();
    void start();
    void event();
};
