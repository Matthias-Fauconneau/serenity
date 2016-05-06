#pragma once
#include "core/image.h"
struct AVFormatContext;
struct SwsContext;
struct AVStream;
struct AVCodecContext;
struct AVFrame;

/// Generic video/audio decoder (using ffmpeg)
struct Decoder {
    union {
        int2 size = 0;
        struct { uint width, height; };
    };
    uint /*timeNum=0,*/ timeDen=0;
    //int firstPTS = 0;

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
    Image read();
    void scale(const Image& image);
    Image scale();
    Image8 Y() const;

    void seek(uint64 videoTime);
};
