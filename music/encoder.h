#pragma once
#include "function.h"
#include "core/image.h"
#include "audio.h" // AudioFile

/// Generic video/audio encoder (using ffmpeg/x264)
struct Encoder {
    String path;

	union { int2 size = 0; struct { uint width, height; }; }; uint64 videoFrameRate=0;
	uint channels = 0; uint audioFrameRate=0;
    struct AVFormatContext* context=0;
    struct SwsContext* swsContext=0;
    struct AVStream* videoStream=0; struct AVCodecContext* videoCodec=0;
    struct AVStream* audioStream=0; struct AVCodecContext* audioCodec=0;
    uint64 videoTime = 0, videoEncodedTime = 0, audioTime = 0, audioEncodedTime = 0;

    /// Starts a new file recording video
    Encoder(string name);
    void setVideo(int2 size, uint videoFrameRate);
    void setAudio(const AudioFile& audio);
	void setAAC(uint channels, uint rate);
	void setFLAC(uint channels, uint rate);
    void open();
    /// Flushes all encoders and close the file
    ~Encoder();
    operator bool() { return context; }

    /// Writes a video frame
    void writeVideoFrame(const Image& image);
    /// Writes an audio frame
	void writeAudioFrame(ref<int16> audio);
	/// Writes an audio frame
	void writeAudioFrame(ref<int32> audio);
};
