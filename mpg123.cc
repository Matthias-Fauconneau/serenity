#include "mpg123.h"
#include "debug.h"
#include <mpg123.h>

void AudioFile::open(const ref<byte>& path) {
    static int unused once=mpg123_init();
    close();
	file = mpg123_new(0,0);
	mpg123_param(file, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);
	if(mpg123_open(file,strz(path))) { file=0; return; }
	long rate; int channels, encoding;
	mpg123_getformat(file,&rate,&channels,&encoding);
    audioInput = __( (uint)rate, (uint)channels );
	assert(audioInput.channels == audioOutput.channels);
	timeChanged(position(),duration());

	if(audioInput.frequency != audioOutput.frequency) {
		new (&resampler) Resampler(audioInput.channels, audioInput.frequency, audioOutput.frequency);
	}
}
void AudioFile::close() { if(file) { mpg123_close(file); mpg123_delete(file); file=0; } }
int AudioFile::position() { return (int)mpg123_tell(file)/audioInput.frequency; }
int AudioFile::duration() { return (int)mpg123_length(file)/audioInput.frequency; }
void AudioFile::seek( int time ) { mpg123_seek_frame(file,mpg123_timeframe(file,time),0); }
void AudioFile::read(int16* output, uint outputSize) {
	if(file) timeChanged(position(),duration());
	if(!file) { clear(output,outputSize*2); return; }
	for(;;) {
		if(inputSize>0) {
			uint size = min((uint)inputSize,outputSize);
			for(uint i=0;i<size*audioOutput.channels;i++)
                output[i] = clip(-32768,int(input[i]*32768),32767); //copy/convert input buffer to output
			inputSize -= size; input += size*audioInput.channels; //update input buffer
			outputSize -= size; output += size*audioOutput.channels; //update output buffer
			if(!inputSize && buffer) { unallocate(buffer,bufferSize); bufferSize=0; } //free consumed buffer
			if(!outputSize) return; //output filled
		}

		//fill input buffer
		if(mpg123_decode_frame(file,0,(uint8**)&input,(size_t*)&inputSize)) {
            close();
			timeChanged(0,0);
			clear(output,outputSize*2);
			return;
		}
        inputSize /= audioInput.channels*sizeof(float);
		assert(inputSize);

		if(resampler) {
			int size = (inputSize*audioOutput.frequency-1)/audioInput.frequency+1;
			buffer = allocate<float>(bufferSize=size*audioOutput.channels);
			int in = inputSize, out = size;
			resampler.filter(input,&in,buffer,&out,false);
			assert(in == (int)inputSize,in,inputSize,out,size);
			input = buffer; inputBufferSize=bufferSize; bufferSize=0; inputSize = out;
		}
	}
}
