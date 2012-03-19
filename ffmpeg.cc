#include "ffmpeg.h"
extern "C" {
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

void AudioFile::open(const string& path) {
    if(file) close(); else { av_register_all(); av_log_set_level(AV_LOG_ERROR); }

    audioPTS=0;
    if(avformat_open_input(&file, strz(path).data(), 0, 0)) { file=0; return; }
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
    assert(audio, path);
    assert(audio->sample_fmt == AV_SAMPLE_FMT_FLT || audio->sample_fmt == AV_SAMPLE_FMT_S16);
    assert(audioInput.frequency && audioOutput.frequency);
    assert(audioInput.channels == audioOutput.channels);
    timeChanged.emit(position(),duration());

    if(audioInput.frequency != audioOutput.frequency) {
        new (&resampler) Resampler(audioInput.channels, audioInput.frequency, audioOutput.frequency);
    }
}
void AudioFile::close() { if(file) avformat_close_input(&file); resampler.~Resampler(); }
int AudioFile::position() { return audioPTS/1000; }
int AudioFile::duration() { return file->duration/1000/1000; }
void AudioFile::seek( int time ) {
    if(file && av_seek_frame(file,-1,time*1000*1000,0) < 0) { warn("Seek Error"_); }
    //avformat_seek_file(file,0,0,time*file->file_size/duration(),file->file_size,AVSEEK_FLAG_BYTE);
}
void AudioFile::read(int16* output, int outputSize) {
    if(file) timeChanged.emit(position(),duration());
    if(!file) { clear(output,outputSize*2); return; }
    for(;;) {
        if(inputSize>0) {
            int size = min((int)inputSize,outputSize);
            for(int i=0;i<size*audioOutput.channels;i++)
                output[i] = clip(-32768,int(input[i]*32768),32767); //copy/convert input buffer to output
            inputSize -= size; input += size*audioInput.channels; //update input buffer
            outputSize -= size; output += size*audioOutput.channels; //update output buffer
            if(!inputSize && buffer) { delete[] buffer; buffer=0; } //free consumed buffer
            if(!outputSize) return; //output filled
        }

        for(;;) { //fill input buffer
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) goto EndOfFile;
            if( file->streams[packet.stream_index]==audioStream ) {
                AVFrame frame; avcodec_get_frame_defaults(&frame); int gotFrame=0;
                int used = avcodec_decode_audio4(audio, &frame, &gotFrame, &packet);
                //av_free_packet(&packet);
                if(used < 0 || !gotFrame) continue;
                inputSize = frame.nb_samples;
                if(audio->sample_fmt == AV_SAMPLE_FMT_FLT) {
                    input = (float*)frame.data[0];
                } else {
                    input = new float[inputSize*audioInput.channels];
                    int16* samples = (int16*)frame.data[0];
                    for(int i=0;i<inputSize*audioInput.channels;i++) input[i] = samples[i]/32768.0;
                }
                audioPTS = packet.dts*audioStream->time_base.num*1000/audioStream->time_base.den;
                break;
            } else av_free_packet(&packet);
        }
        assert(inputSize);

        if(resampler) {
            int size = (inputSize*audioOutput.frequency-1)/audioInput.frequency+1;
            buffer = new float[size*audioOutput.channels];
            int in = inputSize, out = size;
            resampler.filter(input,&in,buffer,&out);
            assert(in == (int)inputSize,in,inputSize,out,size);
            if(audio->sample_fmt != AV_SAMPLE_FMT_FLT) delete[] input;
            input = buffer; inputSize = out;
        }
    }
EndOfFile:
    close();
    timeChanged.emit(0,0);
    clear(output,outputSize*2);
}
