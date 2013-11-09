#pragma once
#include "thread.h"

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

    buffer<int> intBuffer;
    buffer<float> floatBuffer;
    uint bufferIndex=0, bufferSize=0;

    AudioFile();
    ~AudioFile() { close(); }

    operator bool() { return file; }
    bool openPath(const string& path);
    //bool openData(buffer<byte>&& data);
    bool open();
    void close();

    // \note As \a buffer type depends on previous usages of \a read, switching between overloads may only happen when no data is buffered
    uint read(int32* output, uint outputSize);
    uint read(float* output, uint outputSize);

    void seek(uint position);
};

struct Audio {
    uint channels;
    uint rate;
    buffer<int32> data;
};
Audio decodeAudio(const string& path);
//Audio decodeAudio(buffer<byte>&& data);
