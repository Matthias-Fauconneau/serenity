#pragma once
#include "core.h"

/// Generic audio decoder (using ffmpeg)
struct AudioFile {
    static constexpr uint channels = 2;
    uint rate=0;
    struct AVFormatContext* file=0;
    struct AVStream* audioStream=0;
    struct AVCodecContext* audio=0;
    struct AVFrame* frame=0;
    uint audioPTS=0;
    int16* buffer=0;
    uint bufferSize=0;

    bool open(const ref<byte>& path);
    uint read(int16* output, uint outputSize);

    uint position() { return audioPTS/1000;  }
    uint duration();
    void seek(uint position);

    void close();
    ~AudioFile() { close(); }
    operator bool() { return file; }

};
