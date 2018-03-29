#pragma once
#include "function.h"
#include "core/image.h"
struct AVFormatContext;
struct AVStream;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

/// Generic video/audio encoder (using ffmpeg/x264)
struct Encoder {
    String path;

    uint2 size = 0_;
    int videoTimeNum = 0, videoTimeDen = 0;

    handle<AVFormatContext*> file;
    handle<AVStream*> videoStream;
    handle<AVCodecContext*> videoCodec;
    handle<AVFrame*> frame;
    handle<SwsContext*> swsContext;

    /// Starts a new file recording video
    Encoder(string name);

    void setH264(uint2 size, uint videoTimeDen, uint videoTimeNum);
    //void setH265(uint2 size, uint videoTimeDen, uint videoTimeNum);
    //void setAPNG(uint2 size, uint videoFrameRate);
    void open();
    /// Flushes all encoders and close the file
    ~Encoder();
    operator bool() { return file; }

    /// Writes a video frame
    void writeVideoFrame(const Image& image, int64 pts);
    void writeFrame(AVCodecContext* context, const AVStream* stream);
};
