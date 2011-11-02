#pragma once
#include "string.h"
#include "process.h"

struct AudioFormat { int frequency, channels; };

struct AudioInput {
virtual void setup(const AudioFormat& format) =0;
virtual void read(int16* output, int size) =0;
};

struct Resampler {
	int channelCount=0;
	int sourceRate=0;
	int targetRate=0;

	float* kernel=0;
	int N=0;

	float* mem=0;
	int memSize=0;

	int integerAdvance=0;
	int decimalAdvance=0;

	struct {
		int integerIndex=0;
		int decimalIndex=0;
	} channels[8];

	~Resampler();
	void setup(int channels, int sourceRate, int targetRate);
	void filter(const float *source, int *sourceSize, float *target, int *targetSize);
	operator bool();
};

typedef struct mpg123_handle_struct mpg123_handle;
struct AudioFile : AudioInput {
	mpg123_handle* file=0;
	AudioFormat audioInput,audioOutput;
	Resampler resampler;
	float* buffer=0; float* input=0; int inputSize=0;

	void open(const string& path);
	void close();
	int position();
	int duration();
	void seek( int time );
	void setup(const AudioFormat& format);
	void read(int16* output, int size);
	signal(int,int) timeChanged;
};

typedef struct _snd_pcm snd_pcm_t;
typedef unsigned long snd_pcm_uframes_t;
struct AudioOutput : Poll {
	snd_pcm_t* pcm=0;
	snd_pcm_uframes_t period;
	AudioInput* input=0;
	AudioFormat format{48000,2};
	bool running=false;

	AudioOutput();
	pollfd poll();
	bool event(pollfd);
	virtual void setInput(AudioInput* input);
	virtual void start();
	virtual void stop();
};
