#include "string.h"
#include "signal.h"
#include "resample.h"

struct AudioFormat { int frequency, channels; };

struct AudioFile {
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
    signal<int,int> timeChanged;

protected:
    struct AVFormatContext* file=0;
    struct AVStream* audioStream=0;
    struct AVCodecContext* audio=0;
    int audioPTS=0;
};
