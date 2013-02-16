#include "ffmpeg.h"
#include "string.h"

/// Generic audio decoder (using ffmpeg)
extern "C" {
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

bool AudioFile::open(const ref<byte>& path){
    static int unused once=(av_register_all(), 0);
    close();
    if(avformat_open_input(&file, strz(path), 0, 0)) { file=0; return false; }
    avformat_find_stream_info(file, 0);
    if(file->duration <= 0) return false;
    for(uint i=0; i<file->nb_streams; i++) {
        if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
            audioStream = file->streams[i];
            audio = audioStream->codec;
            audio->request_sample_fmt = audio->sample_fmt = AV_SAMPLE_FMT_S16;
            AVCodec* codec = avcodec_find_decoder(audio->codec_id);
            if(codec && avcodec_open2(audio, codec, 0) >= 0 ) {
                this->rate = audio->sample_rate;
                break;
            }
        }
    }
    assert(audio, path);
    assert(audio->sample_rate);
    assert(audio->channels == channels);
    assert(audio->sample_fmt == AV_SAMPLE_FMT_S16 || audio->sample_fmt == AV_SAMPLE_FMT_FLTP, (int)audio->sample_fmt);
    return true;
}

uint AudioFile::read(int16* output, uint outputSize) {
    uint readSize = 0;
    while(readSize<outputSize) {
        if(!bufferSize) {
            Locker lock(avLock);
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) return readSize;
            if(file->streams[packet.stream_index]==audioStream) {
                if(!frame) frame = avcodec_alloc_frame(); int gotFrame=0;
                int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
                if(used < 0 || !gotFrame) continue;
                bufferIndex=0, bufferSize = frame->nb_samples;
                if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
                    buffer = (int16*)frame->data[0];
                } else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                    if(buffer) unallocate(buffer);
                    buffer = allocate<int16>(bufferSize*channels);
                    for(uint i : range(bufferSize)) {
                        buffer[2*i+0] = ((float*)frame->data[0])[i]*0x1.0p15;
                        buffer[2*i+1] = ((float*)frame->data[1])[i]*0x1.0p15;
                    }
                } else error("Unknown format");
                audioPTS = packet.dts*audioStream->time_base.num*1000/audioStream->time_base.den;
            }
            av_free_packet(&packet);
        }
        uint size = min(bufferSize, outputSize-readSize);
        copy(output, buffer+bufferIndex, size*channels);
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
            Locker lock(avLock);
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) return readSize;
            if(file->streams[packet.stream_index]==audioStream) {
                if(!frame) frame = avcodec_alloc_frame(); int gotFrame=0;
                int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
                if(used < 0 || !gotFrame) continue;
                bufferIndex=0, bufferSize = frame->nb_samples;
                if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
                    buffer = (int16*)frame->data[0];
                } else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                    if(buffer) unallocate(buffer);
                    buffer = allocate<int16>(bufferSize*channels);
                    for(uint i : range(bufferSize)) {
                        buffer[2*i+0] = ((float*)frame->data[0])[i]*0x1.0p15;
                        buffer[2*i+1] = ((float*)frame->data[1])[i]*0x1.0p15;
                    }
                } else error("Unknown format");
                audioPTS = packet.dts*audioStream->time_base.num*1000/audioStream->time_base.den;
            }
            av_free_packet(&packet);
        }
        uint size = min(bufferSize, outputSize-readSize);
        copy(output, buffer+bufferIndex, size*channels);
        bufferSize -= size; bufferIndex += size*channels;
        readSize += size; output+= size*channels;
    }
    assert(readSize == outputSize);
    return readSize;
}

uint AudioFile::duration() { return file->duration/1000/1000; }

void AudioFile::seek(uint position) { Locker lock(avLock); av_seek_frame(file,-1,position*1000*1000,0); }

void AudioFile::close() { if(frame) avcodec_free_frame(&frame); if(file) avformat_close_input(&file); }
