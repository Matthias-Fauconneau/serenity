#include "media.h"
extern "C" {
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include "media.h"
#include <mpg123.h>

void AudioFile::setup(const AudioFormat &format) { audioOutput=format; }
void AudioFile::open(const string& path) {
	if(file) close(); else { av_register_all(); av_log_set_level(AV_LOG_ERROR); }

	audioPTS=0;
	if(avformat_open_input(&file, strz(path).data, 0, 0)) { file=0; return; }
	avformat_find_stream_info(file, 0);
	if(file->duration <= 0) { close(); return; }
	for(uint i=0; i<file->nb_streams; i++) {
		if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
			audioStream = file->streams[i];
			audio = audioStream->codec;
			audio->request_sample_fmt = audio->sample_fmt = AV_SAMPLE_FMT_FLT;
			AVCodec* codec = avcodec_find_decoder(audio->codec_id);
			if(codec && avcodec_open2(audio, codec, 0) >= 0 ) {
				audioInput = { audio->sample_rate, audio->channels };
				break;
			}
		}
	}
	assert(audioInput.frequency && (audio->sample_fmt == AV_SAMPLE_FMT_FLT || audio->sample_fmt == AV_SAMPLE_FMT_S16) && audioInput.channels == audioOutput.channels, audioInput.frequency,(int)audio->sample_fmt,audioInput.channels);
	timeChanged.emit(position(),duration());

	if(audioInput.frequency != audioOutput.frequency) {
		new (&resampler) Resampler(audioInput.channels, audioInput.frequency, audioOutput.frequency);
	}
}
void AudioFile::close() { if(file) { av_close_input_file(file); file=0; } }
int AudioFile::position() { return audioPTS/1000; }
int AudioFile::duration() { return file->duration/1000/1000; }
void AudioFile::seek( int time ) {
	if( av_seek_frame(file,-1,time*1000*1000,0) < 0 ) fail("seek");
	//avformat_seek_file(file,0,0,time*file->file_size/duration(),file->file_size,AVSEEK_FLAG_BYTE);
}
void AudioFile::read(int16* output, int outputSize) {
	if(file) timeChanged.emit(position(),duration());
	if(!file) { clear(output,outputSize*2); return; }
	for(;;) {
		if(inputSize>0) {
			int size = min((int)inputSize,outputSize);
			for(int i=0;i<size*audioOutput.channels;i++) output[i] = clip(-32768,int(input[i]*32768),32767); //copy/convert input buffer to output
			inputSize -= size; input += size*audioInput.channels; //update input buffer
			outputSize -= size; output += size*audioOutput.channels; //update output buffer
			if(!inputSize && buffer) delete[] buffer; //free consumed buffer
			if(!outputSize) return; //output filled
		}

		for(;;) { //fill input buffer
			AVPacket packet;
			if(av_read_frame(file, &packet) < 0) { av_free_packet(&packet); timeChanged.emit(duration(),duration()); clear(output,outputSize*2); return; }
			if( file->streams[packet.stream_index]==audioStream ) {
				if(audio->sample_fmt == AV_SAMPLE_FMT_FLT) {
					input = new float[48000];
					inputSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
					int used = avcodec_decode_audio3(audio, (int16*)input, &inputSize, &packet);
					if(used != packet.size) fail("Incomplete");
					inputSize /= audioInput.channels*4;
				} else {
					int16* buffer = new int16[48000*2];
					inputSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
					int used = avcodec_decode_audio3(audio, buffer, &inputSize, &packet);
					if(used != packet.size) fail("Incomplete");
					inputSize /= audioInput.channels*2;
					input = new float[inputSize*audioInput.channels];
					for(int i=0;i<inputSize*audioInput.channels;i++) input[i] = buffer[i]/32768.0;
					delete[] buffer;
				}
				av_free_packet(&packet);
				audioPTS = packet.dts*audioStream->time_base.num*1000/audioStream->time_base.den;
				break;
			}
			av_free_packet(&packet);
		}
		assert(inputSize);

		if(resampler) {
			int size = (inputSize*audioOutput.frequency-1)/audioInput.frequency+1;
			buffer = new float[size*audioOutput.channels];
			int in = inputSize, out = size;
			resampler.filter(input,&in,buffer,&out);
			assert(in == (int)inputSize,in,inputSize,out,size);
			delete[] input;
			input = buffer; inputSize = out;
		}
	}
}
