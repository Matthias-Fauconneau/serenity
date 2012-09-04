#pragma once
#include "function.h"
#include "resample.h"

struct AudioFormat { uint frequency, channels; };

struct AudioFile {
    struct mpg123_handle_struct* file = 0;
    signal<int,int> timeChanged;
    AudioFormat audioInput,audioOutput;
    Resampler resampler;
    float* buffer=0; int bufferSize=0;
    float* input=0; int inputBufferSize=0;
    unsigned long inputSize=0;

    void open(const ref<byte>& file);
    void close();
    int position();
    int duration();
    void seek( int time );
    void setup(const AudioFormat& format);
    void read(int16* output, uint size);
};
