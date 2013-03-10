#pragma once
#include "process.h"

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

    void* buffer=0;
    uint bufferIndex=0, bufferSize=0;

    AudioFile();
    ~AudioFile() { close(); }

    operator bool() { return file; }
    bool openPath(const ref<byte>& path);
    bool openData(array<byte>&& data);
    bool open();
    void close();

    // \note As \a buffer type depends on previous usages of \a read, switching between overloads may only happen when no data is buffered
    uint read(int16* output, uint outputSize);
    uint read(float* output, uint outputSize);

    void seek(uint position);
};

/// Generic audio data type
template<Type T> struct Audio {
    uint channels;
    uint rate;
    buffer<T> data;
};

template<Type T> Audio<T> decodeAudio(array<byte>&& data);
