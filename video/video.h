#pragma once
#include "image.h"
struct AVFormatContext;
struct SwsContext;
struct AVStream;
struct AVCodecContext;
struct AVFrame;

/// Generic video/audio decoder (using ffmpeg)
struct Decoder {
    union {
        uint2 size = 0;
        struct { uint width, height; };
    };
    uint timeDen=0;

    int duration = 0; // in stream time base

    handle<AVFormatContext*> file;
    handle<SwsContext*> swsContext;
    int2 scaledSize = 0;
    handle<AVStream*> videoStream;
    handle<AVCodecContext*> videoCodec;
    int videoTime = 0; // in stream time base
    handle<AVFrame*> frame;

    Decoder() {}
    Decoder(string name);
    default_move(Decoder);
    ~Decoder();
    explicit operator bool() { return file; }

    /// Reads a video frame
    bool read(const Image& image);
    void seek(uint64 videoTime);
};
