#pragma once
#include "function.h"
#include "image.h"

/// Generic video/audio encoder (using ffmpeg/x264)
struct Encoder {
    /// Starts a new file recording video (and audio if enabled)
    Encoder(const string& name, bool audio=false, int width=1280, int height=720, int fps=60, int rate=48000);
    Encoder(const string& name, function<uint(const mref<float2>& output)> readAudio,
            int width=1280, int height=720, int fps=60, int rate=48000) : Encoder(name, true, width, height, fps, rate) {
        this->readAudio = readAudio;
    }

    ~Encoder() { stop(); }
    operator bool() { return context; }
    int2 size() { return int2(width, height); }


    /// Writes a video frame
    void writeVideoFrame(const Image& image);
    /// Writes an audio frame
    void writeAudioFrame(const ref<float2>& audio);
    /// Flushes all encoders and close the file
    void stop();

    /// readAudio will be called back to request an \a audio frame of \a size samples as needed to follow video time
    function<uint(const mref<float2>& output)> readAudio =[](const mref<float2>&){return 0;};

    const uint width, height, fps, rate, audioSize = 1024;
    struct AVFormatContext* context=0;
    struct SwsContext* swsContext=0;
    struct AVStream* videoStream=0; struct AVCodecContext* videoCodec=0;
    struct AVStream* audioStream=0; struct AVCodecContext* audioCodec=0;
    uint videoTime = 0, videoEncodedTime = 0, audioTime = 0, audioEncodedTime = 0;
};
