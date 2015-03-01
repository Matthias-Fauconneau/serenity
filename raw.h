#pragma once
#include "image.h"
#include "file.h"

// CMV12000
struct Raw : ImageF {
    static constexpr int2 size {4096, 3072};
    Map map;
    real exposure;
    int gain, gainDiv;
    int temperature;

    Raw(ref<byte> fileName, bool convert=true) : map(fileName) {
        ref<uint16> file = cast<uint16>(map);
        ref<uint16> registers = file.slice(file.size-128);
        enum { LineCount = 1, ExternExposure = 70, ExposureTime /*71-72[0:7]*/, BlackReferenceColumns = 89,
               Gain = 115, ADCRange, DigitalGain, BitMode, Temperature = 127 };
        assert_(registers[LineCount] == size.y);
        assert_(registers[ExternExposure] == 0);
        uint exposureTime = ((registers[ExposureTime+1]&0xFF) << 16) + registers[ExposureTime] - 1;
        //assert_((registers[BlackReferenceColumns]&(1<<15)) == 0);
        //assert_((registers[Gain]&(1<<3))==0);
        gainDiv = registers[Gain]&(1<<3) ? 3 : 1;
        gain = 1+ref<int>{0, 1, 3, 7}.indexOf(registers[Gain]&0b111);
        assert_(registers[ADCRange]==(3<<8)||127);
        assert_(registers[DigitalGain]==1);
        int bits = ref<int>{12, 10, 8, 0}[registers[BitMode]&0b11];
        assert_(bits == 12);
        int frameOverheadTimeOverlap = (34 * (registers[82] & 0xFF)) + 1;
        real LVDSClock = 300e6; // 300 MHz
        int lineTime = registers[85] + 1;
        assert_(lineTime == 258, lineTime);
        exposure = (exposureTime * lineTime + frameOverheadTimeOverlap) * bits / LVDSClock;
        temperature = registers[Temperature];

        if(convert) { // Converts 16bit integer to 32bit floating point
            new (this) ImageF(size);
            assert_(file.size-128 == Ref::size);
            for(size_t i: range(file.size-128)) at(i) = (float) file[i] / ((1<<16)-1);
        } //else FIXME: read instead of map
    }
};
constexpr int2 Raw::size;
