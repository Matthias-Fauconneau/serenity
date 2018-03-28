#pragma once
#include "image.h"
struct AVFormatContext;
struct SwsContext;
struct AVStream;
struct AVCodecContext;
struct AVFrame;

/// Generic video/audio decoder (using ffmpeg)
struct Decoder {
    uint2 size = 0_;
    uint videoTimeNum = 0, videoTimeDen = 0;

    int duration = 0; // in stream time base

    handle<AVFormatContext*> file;
    handle<SwsContext*> swsContext;
    int2 scaledSize = 0_;
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
    bool read(const Image& image={});
    //Image read() { Image image(size); if(!read(image)) image={}; return image; }

    Image8 YUV(size_t i) const;

    void seek(uint64 videoTime);
};
