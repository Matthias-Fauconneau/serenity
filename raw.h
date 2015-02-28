#pragma once
#include "image.h"
#include "file.h"

// CMV12000
struct Raw : ImageF {
    static constexpr int2 size {4096, 3072};
    Map map;
    real exposure;
    int gain;
    int temperature;

    Raw(ref<byte> fileName, bool convert=true) : map(fileName) {
        ref<uint16> file = cast<uint16>(map);
        ref<uint16> registers = file.slice(file.size-128);
        enum { ExposureTime = 71 /*71-72*/, /*Programmable-gain amplifier*/ Gain = 115, BitMode = 118, Temperature = 127 };
        int bits = ref<int>{12, 10, 8, 0}[registers[BitMode]&0b11];
        int fot_overlap = (34 * (registers[82] & 0xFF)) + 1;
        exposure = (((registers[ExposureTime+1] << 16) + registers[ExposureTime] - 1)*(registers[85] + 1) + fot_overlap) * bits / 300e6;
        gain = 1+ref<int>{0, 1, 3, 7}.indexOf(registers[Gain]&0b111);
        temperature = registers[Temperature];

        if(convert) { // Converts 16bit integer to 32bit floating point
            new (this) ImageF(size);
            assert_(file.size-128 == Ref::size);
            for(size_t i: range(file.size-128)) at(i) = (float) file[i] / ((1<<16)-1);
        } //else FIXME: read instead of map
    }
};
constexpr int2 Raw::size;
