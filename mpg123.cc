#include "media.h"
#include <mpg123.h>

void AudioFile::setup(const AudioFormat &format) { audioOutput=format; }
void AudioFile::open(const string& path) {
	if(file) close(); else mpg123_init();

	file = mpg123_new(0,0);
	mpg123_param(file, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);
	if(mpg123_open(file,strz(path).data)) { file=0; return; }
	long rate; int channels, encoding;
	mpg123_getformat(file,&rate,&channels,&encoding);
	audioInput = { (int)rate, channels };
	assert(audioInput.channels == audioOutput.channels);
	emit(timeChanged,position(),duration());

	if(audioInput.frequency != audioOutput.frequency) {
		resampler.setup(audioInput.channels, audioInput.frequency, audioOutput.frequency);
	}
}
void AudioFile::close() { mpg123_close(file); mpg123_delete(file); file=0; }
int AudioFile::position() { return (int)mpg123_tell(file)/audioInput.frequency; }
int AudioFile::duration() { return (int)mpg123_length(file)/audioInput.frequency; }
void AudioFile::seek( int time ) { mpg123_seek_frame(file,mpg123_timeframe(file,time),0); }
void AudioFile::read(int16* output, int outputSize) {
	if(file) emit(timeChanged,position(),duration());
	if(!file) { clear(output,outputSize*2); return; }
	for(;;) {
		if(inputSize>0) {
			int size = min((int)inputSize,outputSize);
			for(int i=0;i<size*2;i++) { //copy/convert input buffer to output
				int s = int(input[i]*32768); assert(s >= -32768 && s < 32768); output[i] = int16(s);
			}
			inputSize -= size; input += size*audioInput.channels; //update input buffer
			outputSize -= size; output += size*audioOutput.channels; //update output buffer
			if(!inputSize && buffer) delete[] buffer; //free consumed buffer
			if(!outputSize) return; //output filled
		}

		//fill input buffer
		if(mpg123_decode_frame(file,0,(uint8**)&input,(size_t*)&inputSize)) {
			emit(timeChanged,duration(),duration());
			clear(output,outputSize*2);
			return;
		}
		inputSize /= audioInput.channels*4;
		assert(inputSize);

		if(resampler) {
			int size = (inputSize*audioOutput.frequency-1)/audioInput.frequency+1;
			buffer = new float[size*audioOutput.channels];
			int in = inputSize, out = size;
			resampler.filter(input,&in,buffer,&out);
			assert(in == (int)inputSize,in,inputSize,out,size);
			input = buffer; inputSize = out;
		}
	}
}
