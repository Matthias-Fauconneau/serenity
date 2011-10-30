#pragma once
#include "common.h"

struct AudioFormat { int frequency, channels; };

struct AudioInput {
virtual void setup(const AudioFormat& format) =0;
virtual void read(int16* output, int size) =0;
};

struct AudioFile : AudioInput {
	virtual void open(const string& path) =0;
	virtual void close() =0;
	virtual void seek( int time ) =0;
	signal(int,int) timeChanged;
	static AudioFile* instance();
};

struct Resampler {
	virtual void setup(int channels, int sourceRate, int targetRate) =0;
	virtual void filter(const float *source, int *sourceSize, float *target, int *targetSize) =0;
	static Resampler* instance();
};

struct AudioOutput {
	virtual void setInput(AudioInput* input) =0;
	virtual void start() =0;
	virtual void stop() =0;
	static AudioOutput* instance();
};
