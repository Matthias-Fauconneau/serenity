#include "EmbreeUtil.h"

static RTCDevice globalDevice = nullptr;

void initDevice()
{
    globalDevice = rtcNewDevice(nullptr);
}

RTCDevice getDevice()
{
    return globalDevice;
}
