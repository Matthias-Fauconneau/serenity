#include "audio.h"
#include "string.h"

/// Generic audio decoder (using FFmpeg)
extern "C" {
#define _MATH_H // Prevent system <math.h> inclusion which conflicts with local "math.h"
#define _STDLIB_H // Prevent system <stdlib.h> inclusion which conflicts with local "thread.h"
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h> //avformat
#include <libavcodec/avcodec.h> //avcodec
#include <libavutil/avutil.h> //avutil
}

void __attribute((constructor(1001))) initialize_FFmpeg() { av_register_all(); }

AudioFile::AudioFile(string path) {
	if(avformat_open_input(&file, strz(path), 0, 0)) { log("No such file"_, path); return; }
    avformat_find_stream_info(file, 0);
	if(file->duration <= 0) { file=0; log("Invalid file"); return; }
	frame = av_frame_alloc();
    for(uint i=0; i<file->nb_streams; i++) {
        if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
            audioStream = file->streams[i];
            audio = audioStream->codec;
            audio->request_sample_fmt = AV_SAMPLE_FMT_S16;
            AVCodec* codec = avcodec_find_decoder(audio->codec_id);
            if(codec && avcodec_open2(audio, codec, 0) >= 0) {
				channels = audio->channels;
				assert_(channels == 1 || channels == 2);
				audioFrameRate = audio->sample_rate;
				assert_(audioStream->time_base.num == 1, audioStream->time_base.den, audioFrameRate);
				assert_(audioFrameRate%audioStream->time_base.den == 0, audioStream->time_base.den, audioFrameRate);
				if(audioStream->duration != AV_NOPTS_VALUE) {
					assert_(audioStream->duration != AV_NOPTS_VALUE);
					duration = (int64)audioStream->duration*audioFrameRate*audioStream->time_base.num/audioStream->time_base.den;
				} else {
					duration = (int64)file->duration*audioFrameRate/AV_TIME_BASE;

				} /*else { // Explicitly evaluate duration by decoding whole file (FIXME)
					duration = 0;
					for(;;) {
						AVPacket packet;
						if(av_read_frame(file, &packet) < 0) break;
						if(file->streams[packet.stream_index]==audioStream) {
							avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
							duration += frame->nb_samples;
						}
					}
				}*/
				assert_(duration);
                break;
            }
        }
    }
	assert_(audio && audio->sample_rate && (uint)audio->channels == channels &&
            (audio->sample_fmt == AV_SAMPLE_FMT_S16 || audio->sample_fmt == AV_SAMPLE_FMT_S16P ||
             audio->sample_fmt == AV_SAMPLE_FMT_FLTP || audio->sample_fmt == AV_SAMPLE_FMT_S32));
}

size_t AudioFile::read32(mref<int> output) {
    uint readSize = 0;
	while(readSize*channels<output.size) {
        if(!bufferSize) {
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) return readSize;
			if(file->streams[packet.stream_index]==audioStream && packet.pts >= 0 /*FIXME*/) {
				intBuffer = buffer<int>();
				int gotFrame=0;
                int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
                if(used < 0 || !gotFrame) continue;
                bufferIndex=0, bufferSize = frame->nb_samples;
                if(audio->sample_fmt == AV_SAMPLE_FMT_S32) {
					intBuffer = unsafeRef(ref<int>((int*)frame->data[0], bufferSize)); // Valid until next frame
                }
				/*else if(audio->sample_fmt == AV_SAMPLE_FMT_S16P) {
                    intBuffer = buffer<int2>(bufferSize);
                    for(uint i : range(bufferSize)) {
                        intBuffer[i][0] = ((int16*)frame->data[0])[i]<<16;
                        intBuffer[i][1] = ((int16*)frame->data[1])[i]<<16;
                    }
				}*/
                else if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
					intBuffer = buffer<int>(bufferSize*channels);
					for(uint i : range(bufferSize*channels)) intBuffer[i] = ((int16*)frame->data[0])[i] << 16;
                }
				/*else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
					intBuffer = buffer<int2>(bufferSize);
					for(uint i : range(bufferSize)) for(uint j : range(2)) {
						int32 s = ((float*)frame->data[j])[i]*(1<<30); //TODO: ReplayGain
						//if(s<-(1<<31) || s >= (1<<31)) error("Clip", s, ((float*)frame->data[j])[i]);
						intBuffer[i][j] = s;
					}
				}*/
                else error("Unimplemented conversion to int32 from", (int)audio->sample_fmt);
				audioTime = packet.pts*audioFrameRate*audioStream->time_base.num/audioStream->time_base.den;
            }
            av_free_packet(&packet);
        }
		uint size = min(bufferSize, output.size/channels-readSize);
		output.slice(readSize*channels, size*channels).copy(intBuffer.slice(bufferIndex*channels, size*channels));
        bufferSize -= size; bufferIndex += size; readSize += size;
    }
	assert(readSize*channels == output.size);
    return readSize;
}

size_t AudioFile::read(mref<float> output) {
	assert_(channels);
	uint readSize = 0;
	while(readSize < output.size*channels) {
		if(!bufferSize) {
			AVPacket packet;
			if(av_read_frame(file, &packet) < 0) return readSize;
			if(file->streams[packet.stream_index]==audioStream && packet.pts >= 0 /*FIXME*/) {
				if(!frame) frame = av_frame_alloc(); int gotFrame=0;
				int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
				if(used < 0 || !gotFrame) continue;
				bufferIndex=0, bufferSize = frame->nb_samples;
				floatBuffer = buffer<float>(bufferSize*channels);
				if(audio->sample_fmt == AV_SAMPLE_FMT_S32) {
					for(uint i : range(bufferSize*channels)) floatBuffer[i] = ((int32*)frame->data[0])[i]*0x1.0p-31;
				}
				else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
					for(uint i : range(bufferSize)) for(uint c : range(channels)) floatBuffer[i*channels+c] = ((float*)frame->data[c])[i];
				}
				else if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
					for(uint i : range(bufferSize*channels)) floatBuffer[i] = ((int16*)frame->data[0])[i]*0x1.0p-15;
				}
				else error("Unimplemented conversion to float32 from", (int)audio->sample_fmt);
				audioTime = packet.pts*audioFrameRate*audioStream->time_base.num/audioStream->time_base.den;
			}
			av_free_packet(&packet);
		}
		uint size = min(bufferSize, output.size/channels-readSize);
		output.slice(readSize*channels, size*channels).copy(floatBuffer.slice(bufferIndex*channels, size*channels));
		bufferSize -= size; bufferIndex += size; readSize += size;
	}
	assert_(readSize*channels == output.size);
	return readSize;
}

void AudioFile::seek(uint audioTime) {
	assert_(audioStream->time_base.num == 1);
	av_seek_frame(file, audioStream->index, (uint64)audioTime*audioStream->time_base.den/audioFrameRate, 0);
	intBuffer=buffer<int>(); floatBuffer=buffer<float>(); bufferIndex=0, bufferSize=0;
	this->audioTime = audioTime; // FIXME: actual
}

AudioFile::~AudioFile() {
    if(frame) av_frame_free(&frame);
	duration=0; audioStream = 0; audio=0;
	intBuffer=buffer<int>(); floatBuffer=buffer<float>(); bufferIndex=0, bufferSize=0;
    if(file) avformat_close_input(&file);
}

Audio decodeAudio(string path) {
	AudioFile file(path);
	Audio audio (buffer<float>(file.duration*file.channels), file.channels, file.audioFrameRate);
	audio.size = file.read(audio) * file.channels;
	return audio;
}
