#include "stretch.h"
#include <rubberband/RubberBandStretcher.h>

AudioStretch::AudioStretch(uint rate) {
    rubberband = new RubberBand::RubberBandStretcher(rate, 2, RubberBand::RubberBandStretcher::OptionProcessRealTime, 2);
}

uint AudioStretch::read(int16* output, uint size) {
    while((uint)rubberband->available()<size) {
        uint need = rubberband->getSamplesRequired();
        int16 data[need*2];
        need = this->need(data, need);

        float buffer[2][need];
        for(uint i=0; i<need; i++) { // interleaved short to planar float
            buffer[0][i] = data[i*2+0];
            buffer[1][i] = data[i*2+1];
        }

        float* buffers[] = {buffer[0], buffer[1]};
        rubberband->process(buffers, need, false);
    }

    float buffer[2][size];
    float* buffers[] = {buffer[0], buffer[1]};
    uint read = rubberband->retrieve(buffers, size);
    //assert(read == size);
    for(uint i=0; i<read; i++) { // planar float to interleaved short
        output[i*2+0] = buffer[0][i];
        output[i*2+1] = buffer[1][i];
    }
    return read;
}
