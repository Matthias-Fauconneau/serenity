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

    AudioFile(){}
    AudioFile(const string path) { open(path); }
    default_move(AudioFile);
    ~AudioFile() { close(); }

	explicit operator bool() { return file; }
    bool open(const string path);
    bool open();
    void close();

	size_t read16(mref<short2> output);
	size_t read32(mref<int2> output);

    void seek(uint position);
};
