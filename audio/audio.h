#pragma once
#include "thread.h"
#include "vector.h"

/// Generic audio file
struct AudioFile {
	uint channels = 0;
	uint audioFrameRate = 0;
	int64 audioTime = 0;
    uint duration = 0;

    virtual ~AudioFile() {}

    virtual size_t read16(mref<int16> output) abstract;
    virtual size_t read32(mref<int32> output) abstract;
    virtual size_t read(mref<float> output) abstract;

    virtual void seek(uint audioTime) abstract;
};

/// Generic audio decoder using ffmpeg
struct FFmpeg : AudioFile {
    struct AVFormatContext* file=0;
    struct AVStream* audioStream=0;
    struct AVCodecContext* audio=0;
    enum Codec { Invalid=-1, AAC, FLAC } codec;
    struct AVFrame* frame=0;

    buffer<int16> int16Buffer;
    buffer<int32> int32Buffer;
    buffer<float> floatBuffer;
    size_t bufferIndex=0, bufferSize=0;

    FFmpeg(string path);
    default_move(FFmpeg);
    ~FFmpeg();

    size_t read16(mref<int16> output) override;
    size_t read32(mref<int32> output) override;
    size_t read(mref<float> output) override;

    void seek(uint audioTime) override;
};

struct Audio : buffer<float> {
    Audio() {}
    Audio(buffer<float>&& data, uint channels, uint rate) : buffer<float>(::move(data)), channels(channels), rate(rate) {}
    uint channels=0, rate=0;
};

Audio /*__attribute((weak))*/ decodeAudio(string /*path*/);// { error("FFmpeg support not linked"); }
