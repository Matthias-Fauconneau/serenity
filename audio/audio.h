#pragma once
#include "thread.h"
#include "vector.h"

/// Generic audio decoder (using ffmpeg)
struct AudioFile {
	uint channels = 0;
	uint audioFrameRate = 0;
	int64 audioTime = 0;
    uint duration = 0;

    struct AVFormatContext* file=0;
    struct AVStream* audioStream=0;
    struct AVCodecContext* audio=0;
	enum Codec { Invalid=-1, AAC, FLAC } codec;
    struct AVFrame* frame=0;

	buffer<int16> int16Buffer;
	buffer<int32> int32Buffer;
	buffer<float> floatBuffer;
    size_t bufferIndex=0, bufferSize=0;

	AudioFile(){}
	AudioFile(string path);
	default_move(AudioFile);
	~AudioFile();
	explicit operator bool() { return file; }

	size_t read16(mref<int16> output);
	size_t read32(mref<int32> output);
	size_t read(mref<float> output);

	void seek(uint audioTime);
};

struct Audio : buffer<float> {
	Audio() {}
	Audio(buffer<float>&& data, uint channels, uint rate) : buffer<float>(::move(data)), channels(channels), rate(rate) {}
	uint channels=0, rate=0;
};
Audio decodeAudio(string path);
