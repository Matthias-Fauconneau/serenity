#pragma once
#include "function.h"
#include "core/image.h"

/// Generic video/audio encoder (using ffmpeg/x264)
struct Encoder {
    String path;

    uint2 size = 0_;
    uint videoTimeNum = 0, videoTimeDen = 0;

    struct AVFormatContext* context = nullptr;
    struct AVStream* videoStream = nullptr;
    struct AVCodecContext* videoCodec = nullptr;
    int videoTime = 0, videoEncodeTime = 0;
    struct AVFrame* frame = nullptr;
    struct SwsContext* swsContext = nullptr;

    /// Starts a new file recording video
    Encoder(string name);

    void setH264(uint2 size, uint videoTimeDen, uint videoTimeNum);
    //void setH265(uint2 size, uint videoTimeDen, uint videoTimeNum);
    //void setAPNG(uint2 size, uint videoFrameRate);
    void open();
    /// Flushes all encoders and close the file
    ~Encoder();
    operator bool() { return context; }

    /// Writes a video frame
    void writeVideoFrame(const Image& image);
};
