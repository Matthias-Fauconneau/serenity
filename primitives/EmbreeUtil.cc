#include "EmbreeUtil.h"

namespace Tungsten {

namespace EmbreeUtil {

static RTCDevice globalDevice = nullptr;

void initDevice()
{
    globalDevice = rtcNewDevice(nullptr);
}

RTCDevice getDevice()
{
    return globalDevice;
}

}

}
