#include "core.h"

struct Resampler {
    no_copy(Resampler)
    Resampler(){}
    Resampler(int channels, int sourceRate, int targetRate);
    ~Resampler();
    void filter(const float *source, int *sourceSize, float *target, int *targetSize, bool mix);
    operator bool() const;
private:
    int sourceRate=0;
    int targetRate=0;

    float* kernel=0;
    float* mem=0;
    int N=0;
    int memSize=0;

    int integerAdvance=0;
    int decimalAdvance=0;

    int channelCount=0;
    struct {
        int integerIndex=0;
        int decimalIndex=0;
    } channels[2];
};
