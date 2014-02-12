#pragma once
#include "thread.h"
#include "vector.h"

/// Generic audio decoder (using ffmpeg)
struct AudioFile {
    static constexpr uint channels = 2;
    uint rate=0;
    uint position = 0;
    uint duration = 0;

    struct AVFormatContext* file=0;
    struct AVStream* audioStream=0;
    struct AVCodecContext* audio=0;
    struct AVFrame* frame=0;

    buffer<short2> shortBuffer;
    buffer<int2> intBuffer;
    buffer<float2> floatBuffer;
    size_t bufferIndex=0, bufferSize=0;

    AudioFile(const string& path);
    ~AudioFile() { close(); }

    operator bool() { return file; }
    bool open();
    void close();

    uint read(const mref<short2>& output);
    uint read(const mref<int2>& output);
    uint read(const mref<float2>& output);

    void seek(uint position);
};

struct Audio : buffer<int2> {
    Audio(buffer&& data, uint rate):buffer(move(data)),rate(rate){}
    uint rate;
};
Audio decodeAudio(const string& path, uint duration=-1);
