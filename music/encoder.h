#pragma once
#include "function.h"
#include "image.h"

/// Generic video/audio encoder (using ffmpeg/x264)
struct Encoder {
    /// Configures for recording, does nothing until #start
    Encoder(int width=1280, int height=720, int fps=30, int rate=48000) : width(width), height(height), fps(fps), rate(rate){}
    Encoder(function<uint(const mref<short2>& output)> readAudio, int width=1280, int height=720, int fps=30, int rate=48000)
        : readAudio(readAudio), width(width), height(height), fps(fps), rate(rate){}
    ~Encoder() { stop(); }
    operator bool() { return context; }
    int2 size() { return int2(width, height); }

    /// Starts a new file recording \a video and/or \a audio
    void start(const string& name, bool video=true, bool audio=true);
    /// Writes a video frame
    void writeVideoFrame(const Image& image);
    /// Writes an audio frame
    void writeAudioFrame(const ref<short2>& audio);
    /// Flushes all encoders and close the file
    void stop();

    /// readAudio will be called back to request an \a audio frame of \a size samples as needed to follow video time
    function<uint(const mref<short2>& output)> readAudio = [](const mref<short2>&){return 0;};

    uint width, height, fps, rate;
    struct AVFormatContext* context=0;
    struct AVStream* videoStream=0; struct AVCodecContext* videoCodec=0;
    struct AVStream* audioStream=0; struct AVCodecContext* audioCodec=0;
    struct SwsContext* swsContext=0;
    uint videoTime = 0, videoEncodedTime = 0, audioTime = 0, audioEncodedTime;
};
