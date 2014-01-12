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

bool AudioFile::openPath(const string& path) {
    close();
    if(avformat_open_input(&file, strz(path), 0, 0)) { log("No such file"_, path);  file=0; return false; }
    log(path);
    return open();
}

bool AudioFile::open() {
    avformat_find_stream_info(file, 0);
    if(file->duration <= 0) { file=0; log("Invalid file"_); return false; }
    for(uint i=0; i<file->nb_streams; i++) {
        if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
            audioStream = file->streams[i];
            audio = audioStream->codec;
            AVCodec* codec = avcodec_find_decoder(audio->codec_id);
            if(codec && avcodec_open2(audio, codec, 0) >= 0) {
                rate = audio->sample_rate;
                duration = audioStream->duration*audioStream->time_base.num*rate/audioStream->time_base.den;
                break;
            }
        }
    }
    assert_(audio);
    assert_(audio->sample_rate);
    assert_((uint)audio->channels == channels);
    assert_(audio->sample_fmt == AV_SAMPLE_FMT_S16 || audio->sample_fmt == AV_SAMPLE_FMT_S16P
            || audio->sample_fmt == AV_SAMPLE_FMT_FLTP || audio->sample_fmt == AV_SAMPLE_FMT_S32, (int)audio->sample_fmt);
    string sampleFormats[] = {"8bit"_,"16bit"_,"32bit"_,"24bit float"_,"48bit float"_,"8bit planar"_,"16bit planar"_,"32bit planar"_,"24bit float planar"_};
    log(audio->sample_rate,"Hz", channels==2?"stereo"_:""_,sampleFormats[audio->sample_fmt]);
    return true;
}

uint AudioFile::read(const mref<int2>& output) {
    uint readSize = 0;
    while(readSize<output.size) {
        if(!bufferSize) {
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) return readSize;
            if(file->streams[packet.stream_index]==audioStream) {
                intBuffer = buffer<int2>();
                if(!frame) frame = avcodec_alloc_frame(); int gotFrame=0;
                int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
                if(used < 0 || !gotFrame) continue;
                bufferIndex=0, bufferSize = frame->nb_samples;
                if(audio->sample_fmt == AV_SAMPLE_FMT_S32) {
                    intBuffer = unsafeReference(ref<int2>((int2*)frame->data[0], bufferSize)); // Valid until next frame
                }
                /*else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                    error("AV_SAMPLE_FMT_FLTP"_);
                    intBuffer = buffer<int2>(bufferSize);
                    for(uint i : range(bufferSize)) for(uint j : range(2)) {
                        int s = ((float*)frame->data[j])[i]*(1<<29);
                        if(s<-(1<<30) || s >= (1<<30)) error("Clip", s);
                        intBuffer[i][j] = s;
                    }
                }*/
                else if(audio->sample_fmt == AV_SAMPLE_FMT_S16P) {
                    intBuffer = buffer<int2>(bufferSize);
                    for(uint i : range(bufferSize)) {
                        intBuffer[i][0] = ((int16*)frame->data[0])[i]<<16;
                        intBuffer[i][1] = ((int16*)frame->data[1])[i]<<16;
                    }
                }
                /*else if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
                    error("AV_SAMPLE_FMT_S16"_);
                    intBuffer = buffer<int>(bufferSize*channels);
                    for(uint i : range(bufferSize*channels)) intBuffer[i] = ((int16*)frame->data[0])[i]<<16;
                }*/
                else error("Unimplemented conversion to int32 from", (int)audio->sample_fmt);
                position = packet.dts*audioStream->time_base.num*rate/audioStream->time_base.den;
            }
            av_free_packet(&packet);
        }
        uint size = min(bufferSize, output.size-readSize);
        copy(output.slice(readSize, size), intBuffer.slice(bufferIndex, size));
        bufferSize -= size; bufferIndex += size; readSize += size;
    }
    assert(readSize == output.size);
    return readSize;
}

uint AudioFile::read(const mref<float2>& output) {
    uint readSize = 0;
    while(readSize<output.size) {
        if(!bufferSize) {
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) return readSize;
            if(file->streams[packet.stream_index]==audioStream) {
                if(!frame) frame = avcodec_alloc_frame(); int gotFrame=0;
                int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
                if(used < 0 || !gotFrame) continue;
                bufferIndex=0, bufferSize = frame->nb_samples;
                floatBuffer = buffer<float2>(bufferSize);
                if(audio->sample_fmt == AV_SAMPLE_FMT_S32) {
                    for(uint i : range(bufferSize)) {
                        floatBuffer[i][0] = ((int32*)frame->data[0])[i*2+0]*0x1.0p-31;
                        floatBuffer[i][1] = ((int32*)frame->data[0])[i*2+1]*0x1.0p-31;
                    }
                }
                else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
                    for(uint i : range(bufferSize)) {
                        floatBuffer[i][0] = ((float*)frame->data[0])[i];
                        floatBuffer[i][1] = ((float*)frame->data[1])[i];
                    }
                }
                else if(audio->sample_fmt == AV_SAMPLE_FMT_S16P) {
                    for(uint i : range(bufferSize)) {
                        floatBuffer[i][0] = ((int16*)frame->data[0])[i]*0x1.0p-15;
                        floatBuffer[i][1] = ((int16*)frame->data[1])[i]*0x1.0p-15;
                    }
                }
                /*else if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
                    for(uint i : range(bufferSize*channels)) {
                        floatBuffer[i] = ((int16*)frame->data[0])[i]*0x1.0p-15;
                    }
                }*/
                else error("Unimplemented conversion to float32 from", (int)audio->sample_fmt);
                position = packet.dts*audioStream->time_base.num*rate/audioStream->time_base.den;
            }
            av_free_packet(&packet);
        }
        uint size = min(bufferSize, output.size-readSize);
        copy(output.slice(readSize, size), floatBuffer.slice(bufferIndex, size));
        bufferSize -= size; bufferIndex += size; readSize += size;
    }
    assert(readSize == output.size);
    return readSize;
}

void AudioFile::seek(uint position) { av_seek_frame(file, audioStream->index, (uint64)position*audioStream->time_base.den/(rate*audioStream->time_base.num), 0); }

void AudioFile::close() {
    if(frame) avcodec_free_frame(&frame);
    rate=0, position=0, duration=0; audioStream = 0; audio=0;
    intBuffer=buffer<int2>(); floatBuffer=buffer<float2>(); bufferIndex=0, bufferSize=0;
    if(file) avformat_close_input(&file);
}

Audio decodeAudio(const string& path, uint duration) {
    AudioFile file; file.openPath(path);
    duration = min(duration, file.duration);
    Audio audio {buffer<int2>(duration), file.rate};
    audio.size = file.read(audio);
    return audio;
}
