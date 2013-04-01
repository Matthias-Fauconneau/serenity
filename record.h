#pragma once
#include "core.h"

struct Record {
    /// Configures for recodring, does nothing until #start
    Record(int width=1280, int height=720, int fps=30, int rate=44100):width(width), height(height), fps(fps), rate(rate){}
    ~Record() { stop(); }

    /// Starts a new file recording \a video and/or \a audio
    void start(const ref<byte>& name, bool video=true, bool audio=false);
    /// Captures current window and record to current file
    void captureVideoFrame();
    /// Captures given audio frame and record to current file
    void captureAudioFrame(float* audio, uint audioSize);
    /// Captures given audio frame and record to current file, also captures current window as necessary to keep framerate
    void capture(float* audio, uint audioSize);
    /// Flushes all encoders and close the file
    void stop();

    uint width, height, fps, rate;
    struct AVFormatContext* context=0;
    struct AVStream* videoStream=0; struct AVCodecContext* videoCodec=0;
    struct AVStream* audioStream=0; struct AVCodecContext* audioCodec=0;
    struct SwsContext* swsContext=0;
    uint videoTime = 0, videoEncodedTime = 0;
    uint audioTime = 0;
};
