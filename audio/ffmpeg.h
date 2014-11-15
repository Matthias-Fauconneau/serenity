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
    struct AVFrame* frame=0;

	buffer<int> intBuffer;
	buffer<float> floatBuffer;
    size_t bufferIndex=0, bufferSize=0;

    AudioFile(){}
    AudioFile(const string path) { open(path); }
    default_move(AudioFile);
    ~AudioFile() { close(); }

	explicit operator bool() { return file; }
    bool open(const string path);
    bool open();
    void close();

	size_t read32(mref<int> output);
	size_t read(mref<float> output);

	void seek(uint audioTime);
};

struct Audio : buffer<float> {
	Audio(buffer<float>&& data, uint rate) : buffer<float>(::move(data)), rate(rate) {}
	uint rate;
};
Audio decodeAudio(string path);
