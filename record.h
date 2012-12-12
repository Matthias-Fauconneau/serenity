#pragma once
#include "core.h"

extern "C" {
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

struct Record {
    uint width=1280, height=720, fps=30;

    AVFormatContext* context;
    AVStream* videoStream;
    AVCodecContext* videoCodec;
    AVStream* audioStream;
    AVCodecContext* audioCodec;
    SwsContext* swsContext;

    uint videoTime = 0, audioTime = 0;
    uint videoEncodedTime = 0;

    bool record=false;
    operator bool() const { return record; }
    void start(const ref<byte>& name);
    void capture(float* audio, uint audioSize);
    void stop();
    ~Record() { stop(); }
};
