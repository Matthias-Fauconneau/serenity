#pragma once
typedef unsigned int uint;
typedef short int16;
namespace RubberBand { struct RubberBandStretcher; }

struct AudioStretch {
    RubberBand::RubberBandStretcher* rubberband;
    AudioStretch(uint rate);
    void setTimeRatio(float ratio);
    virtual uint need(int16* data, uint size)=0;
    uint read(int16* data, uint size);
};
