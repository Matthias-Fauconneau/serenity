#include "ffmpeg.h"
#include "string.h"

/// Generic audio decoder (using ffmpeg)
extern "C" {
#define _MATH_H // Prevent system <math.h> inclusion which conflicts with local "math.h"
#define _STDLIB_H // Prevent system <stdlib.h> inclusion which conflicts with local "thread.h"
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h> //avformat
#include <libswscale/swscale.h> //swscale
#include <libavcodec/avcodec.h> //avcodec
#include <libavutil/avutil.h> //avutil
}

AudioFile::AudioFile() { static int unused once=(av_register_all(), 0); }

bool AudioFile::openPath(const ref<byte>& path) {
    close();
    if(avformat_open_input(&file, strz(path), 0, 0)) { file=0; return false; }
    return open();
}

bool AudioFile::openData(array<byte>&& data) {
    close();
    file = avformat_alloc_context();
    file->pb = avio_alloc_context((uint8*)data.data, data.size, 0, 0, 0, 0, 0);
    data.data=0; data.capacity=data.size=0; // data is now owned by ffmpeg
    if(avformat_open_input(&file, 0, 0, 0)) { file=0; return false; }
    return open();
}
bool AudioFile::open() {
    avformat_find_stream_info(file, 0);
    if(file->duration <= 0) { file=0; return false; }
    for(uint i=0; i<file->nb_streams; i++) {
        if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
            audioStream = file->streams[i];
            audio = audioStream->codec;
            audio->request_sample_fmt = audio->sample_fmt = AV_SAMPLE_FMT_S16; //TODO: S16P
            AVCodec* codec = avcodec_find_decoder(audio->codec_id);
            if(codec && avcodec_open2(audio, codec, 0) >= 0) {
                rate = audio->sample_rate;
                duration = audioStream->duration*audioStream->time_base.num*rate/audioStream->time_base.den;
                break;
            }
        }
    }
    assert(audio);
    assert(audio->sample_rate);
    assert((uint)audio->channels == channels);
    assert(audio->sample_fmt == AV_SAMPLE_FMT_S16 || audio->sample_fmt == AV_SAMPLE_FMT_FLTP, (int)audio->sample_fmt);
    return true;
}

uint AudioFile::read(int32* output, uint outputSize) {
    uint readSize = 0;
    while(readSize<outputSize) {
        if(!bufferSize) {
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) return readSize;
            if(file->streams[packet.stream_index]==audioStream) {
                if(!frame) frame = avcodec_alloc_frame(); int gotFrame=0;
                int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
                if(used < 0 || !gotFrame) continue;
                bufferIndex=0, bufferSize = frame->nb_samples;
                if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
                    intBuffer = buffer<int>(bufferSize*channels);
                    for(uint i : range(bufferSize*channels)) intBuffer[i] = ((int16*)frame->data[0])[i]<<8;
                } else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                    intBuffer = buffer<int>(bufferSize*channels);
                    for(uint i : range(bufferSize)) for(uint j : range(2)) {
                        int s = ((float*)frame->data[j])[i]*(1<<29);
                        if(s<-(1<<30) || s >= (1<<30)) error("Clip", s);
                        intBuffer[2*i+j] = s;
                    }
                } else error("Unknown format");
                position = packet.dts*audioStream->time_base.num*rate/audioStream->time_base.den;
            }
            av_free_packet(&packet);
        }
        uint size = min(bufferSize, outputSize-readSize);
        rawCopy(output, intBuffer+bufferIndex, size*channels);
        bufferSize -= size; bufferIndex += size*channels;
        readSize += size; output+= size*channels;
    }
    assert(readSize == outputSize);
    return readSize;
}

uint AudioFile::read(float* output, uint outputSize) {
    uint readSize = 0;
    while(readSize<outputSize) {
        if(!bufferSize) {
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) return readSize;
            if(file->streams[packet.stream_index]==audioStream) {
                if(!frame) frame = avcodec_alloc_frame(); int gotFrame=0;
                int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
                if(used < 0 || !gotFrame) continue;
                bufferIndex=0, bufferSize = frame->nb_samples;
                floatBuffer = buffer<float>(bufferSize*channels);
                if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
                    for(uint i : range(bufferSize*channels)) {
                        floatBuffer[i] = ((int16*)frame->data[0])[i]*0x1.0p-15;
                    }
                } else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                    for(uint i : range(bufferSize)) {
                        floatBuffer[2*i+0] = ((float*)frame->data[0])[i];
                        floatBuffer[2*i+1] = ((float*)frame->data[1])[i];
                    }
                } else error("Unknown format");
                position = packet.dts*audioStream->time_base.num*rate/audioStream->time_base.den;
            }
            av_free_packet(&packet);
        }
        uint size = min(bufferSize, outputSize-readSize);
        rawCopy(output, floatBuffer+bufferIndex, size*channels);
        bufferSize -= size; bufferIndex += size*channels;
        readSize += size; output+= size*channels;
    }
    assert(readSize == outputSize);
    return readSize;
}

void AudioFile::seek(uint position) { av_seek_frame(file, audioStream->index, (uint64)position*audioStream->time_base.den/(rate*audioStream->time_base.num), 0); }

void AudioFile::close() { if(frame) avcodec_free_frame(&frame); if(file) avformat_close_input(&file); }

template<Type T> Audio<T> decodeAudio(array<byte>&& data) {
    AudioFile file; file.openData(move(data));
    uint size = file.duration*file.channels;
    Audio<T> audio {file.channels, file.rate, buffer<T>(size)};
    audio.data.size = file.read(audio.data.begin(), size);
    return audio;
}
template Audio<float> decodeAudio(array<byte>&& data);
