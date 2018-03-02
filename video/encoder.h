#pragma once
#include "function.h"
#include "core/image.h"

/// Generic video/audio encoder (using ffmpeg/x264)
struct Encoder {
    String path;

    union { uint2 size = 0; struct { uint width, height; }; };
    uint videoFrameRateNum=0, videoFrameRateDen=0;
    struct AVFormatContext* context=0;
    struct AVStream* videoStream=0; struct AVCodecContext* videoCodec=0;
    int videoTime = 0, videoEncodeTime = 0;
    struct AVFrame* frame = 0;
    struct SwsContext* swsContext=0;

    /// Starts a new file recording video
    Encoder(string name);

    void setH264(uint2 size, uint videoFrameRate);
    void setH265(uint2 size, uint videoFrameRate);
    void setAPNG(uint2 size, uint videoFrameRate);
    void open();
    /// Flushes all encoders and close the file
    ~Encoder();
    operator bool() { return context; }

    /// Writes a video frame
    void writeVideoFrame(const Image& image);
};
