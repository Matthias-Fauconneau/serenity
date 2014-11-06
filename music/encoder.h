#pragma once
#include "function.h"
#include "image.h"
#include "ffmpeg.h" // AudioFile

/// Generic video/audio encoder (using ffmpeg/x264)
struct Encoder {
    /// Starts a new file recording video and audio
	Encoder(const string& name, int2 size, int fps, const AudioFile& audio);
    /// Flushes all encoders and close the file
    ~Encoder();
    operator bool() { return context; }

    /// Writes a video frame
    void writeVideoFrame(const Image& image);
    /// Writes an audio frame
    void writeAudioFrame(const ref<float2>& audio);

	union {
		const int2 size = 0;
		struct { const uint width, height; };
	};
	const uint fps, rate, audioSize = 1024;
    struct AVFormatContext* context=0;
    struct SwsContext* swsContext=0;
    struct AVStream* videoStream=0; struct AVCodecContext* videoCodec=0;
    struct AVStream* audioStream=0; struct AVCodecContext* audioCodec=0;
    uint videoTime = 0, videoEncodedTime = 0, audioTime = 0, audioEncodedTime = 0;
};
