#pragma once
#include "core/image.h"

/// Generic video/audio decoder (using ffmpeg)
struct Decoder {
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
	int64 videoTime = 0;
	struct AVFrame* frame=0;

	Decoder() {}
	Decoder(string name);
	default_move(Decoder);
	~Decoder();
	explicit operator bool() { return file; }

	/// Reads a video frame
	Image read();

	void seek(uint64 videoTime);
};
