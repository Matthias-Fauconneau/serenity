#include "asound.h"
#include "encoder.h"

struct Record {
	AudioInput audio {audioInput};
	Record() {
		Encoder encoder {name};
		encoder.setVideo(int2(1280,720), 60);
		encoder.setFLAC(1, audio.rate);
		encoder.open();
	}
	uint audioInput(ref<int2> input) {
		encoder.writeAudio(input);
		return input.size;
	}
} app ("test.mkv");
