#pragma once
#include "image.h"

// CMV12000
struct Raw : ImageF {
    static constexpr int2 size {4096, 3072};
    real exposure;
    int gain, gainDiv;
    int temperature;
	buffer<uint16> registers;

	Raw(ref<byte> file, bool convert=true) : Raw(cast<uint16>(file), convert) {}
	Raw(ref<uint16> data, bool convert=true) {
        if(convert) { // Converts 16bit integer to 32bit floating point
            new (this) ImageF(size);
			assert_(data.size >= Ref::size);
			for(size_t i: range(Ref::size)) at(i) = (float) data[i] / ((1<<16)-1);
        } //else FIXME: read instead of map
		ref<uint16> registers = data.slice(size.y*size.x);
		if(registers) {
			assert_(registers.size == 128, registers.size);
			enum { LineCount = 1, ExternExposure = 70, ExposureTime /*71-72[0:7]*/, BlackReferenceColumns = 89,
				   Gain = 115, ADCRange, DigitalGain, BitMode, Temperature = 127 };
			if(registers[LineCount] != size.y) log("Line count:", registers[LineCount], "!= size.y:",  size.y);
			assert_(registers[LineCount] == size.y, registers[LineCount], size.y, registers);
			assert_(registers[ExternExposure] == 0);
			uint exposureTime = ((registers[ExposureTime+1]&0xFF) << 16) + registers[ExposureTime] - 1;
			assert_((registers[BlackReferenceColumns]&(1<<15)) == 0);
			assert_((registers[Gain]&(1<<3))==0);
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
			this->registers = copyRef(registers);
		}
    }
};
constexpr int2 Raw::size;
