#include "asound.h"
#include "encoder.h"

struct Record {
	AudioInput audio {{this, &Record::audioInput}, 2, 96000, 8192};
	Encoder encoder {arguments()[0]+".mkv"_};
	Record() {
		encoder.setVideo(int2(1280,720), 60);
		encoder.setFLAC(1, audio.rate);
		encoder.open();
		assert_(audio.periodSize <= encoder.audioFrameSize);
	}
	uint audioInput(ref<int32> input) {
		// Downmix
		assert_(audio.channels==2 && encoder.channels==1);
		buffer<int32> mono (input.size/audio.channels);
		for(size_t i: range(mono.size)) mono[i] = (input[i*2+0] + input[i*2+1]) / 2; // Assumes 1bit headroom
		assert_(mono.size <= encoder.audioFrameSize);
		encoder.writeAudioFrame(mono);
		return input.size;
	}
} app;
