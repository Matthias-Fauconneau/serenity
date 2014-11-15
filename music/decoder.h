#pragma once
#include "core/image.h"

/// Generic video/audio decoder (using ffmpeg)
struct Decoder {
	static constexpr uint channels = 2;
	uint rate=0;

	union {
		int2 size = 0;
		struct { uint width, height; };
	};
	int videoFrameRate=0;
	int64 firstPTS = 0;

	uint duration = 0;

	struct AVFormatContext* file=0;
	struct SwsContext* swsContext=0;
	struct AVStream* videoStream=0; struct AVCodecContext* video=0;
	struct AVStream* audioStream=0; struct AVCodecContext* audio=0;
	int64 videoTime = 0, audioTime = 0;
	struct AVFrame* frame=0;

	buffer<short2> shortBuffer;
	size_t bufferIndex=0, bufferSize=0;

	Decoder() {}
	Decoder(string name);
	default_move(Decoder);
	~Decoder();
	explicit operator bool() { return file; }

	/// Reads a video frame
	bool read(const Image& image);
	/// Reads an audio frame
	//size_t read(mref<short2> audio);
};
