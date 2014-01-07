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

    buffer<int2> intBuffer;
    buffer<float2> floatBuffer;
    uint bufferIndex=0, bufferSize=0;

    AudioFile();
    ~AudioFile() { close(); }

    operator bool() { return file; }
    bool openPath(const string& path);
    //bool openData(buffer<byte>&& data);
    bool open();
    void close();

    uint read(const mref<int2>& output);
    uint read(const mref<float2>& output);

    void seek(uint position);
};

/*struct Audio {
    uint channels;
    uint rate;
    buffer<int2> data;
};
Audio decodeAudio(const string& path, uint duration=-1);*/
//Audio decodeAudio(buffer<byte>&& data);
