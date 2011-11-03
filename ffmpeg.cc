#include "media.h"
extern "C" {
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

struct SpeexResamplerState;
SpeexResamplerState *speex_resampler_init(uint nb_channels, uint in_rate, uint out_rate, int quality, int *err);
int speex_resampler_process_interleaved_float(SpeexResamplerState *st,const float *in,uint *in_len,float *out,uint *out_len);

/// Decodes audio/video files using libavcodec/libavformat.
class(FFmpeg, AudioInput) {
    void open(const string& path) {
        av_register_all();
        av_log_set_level(AV_LOG_WARNING);
        if(file) close();
        audioPTS=0;
        if(avformat_open_input(&file, strz(path).data, 0, 0)) { file=0; return; }
        avformat_find_stream_info(file, 0);
        if(file->duration <= 0) { close(); return; }
        for( uint i=0; i<file->nb_streams; i++ ) {
            if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
                audioStream = file->streams[i];
                audio = audioStream->codec;
                audio->sample_fmt = AV_SAMPLE_FMT_FLT;
                AVCodec* codec = avcodec_find_decoder(audio->codec_id);
                if( codec && avcodec_open2(audio, codec, 0) >= 0 ) {
                    audioInput = { audio->sample_rate, audio->channels };
                    if( !audioInput.frequency ) { close(); return; }
                    assert(audio->sample_fmt == AV_SAMPLE_FMT_S16,path);
                }
            }
        }
    }
    void close() { if(file) { av_close_input_file(file); file=0; } }
    void start(const AudioFormat &format) { audioOutput=format; }
    int position() { return audioPTS; }
    int duration() { return file->duration/1000; }
    void seek( int time ) {
        assert( av_seek_frame(file,-1,time*1000,0) >= 0 );
        //avformat_seek_file(file,0,0,time*file->file_size/duration(),file->file_size,AVSEEK_FLAG_BYTE);
    }
    float* read(int size) {
        assert(buffer,"Need destination buffer");
        for(;;) {
            AVPacket packet;
            if(av_read_frame(file, &packet) < 0) { av_free_packet(&packet); emit(update,audioPTS=duration()); return 0; }
            if( file->streams[packet.stream_index]==audioStream ) {
                AVPacket pkt=packet;
                //while( pkt.size > 0 ) {
                int frameSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
                int16* frame = malloc(frameSize);
                int used = avcodec_decode_audio3( audio, (int16*)frame, &frameSize, &pkt );
                if(frequency!=48000)
                assert(bufferSize==size*2*2);
                /*pkt.size -= used;
                    pkt.data += used;
                    if( bufferSize ) {*/
                /*if(pkt.dts>=0) audioPTS = pkt.dts*av_q2d(audioStream->time_base)*1000;
                        else audioPTS=pkt.pos*duration()/file->file_size;*/
                av_free_packet(&packet);
                return buffer;
                /*}
                }*/
            }
            av_free_packet(&packet);
        }
    }

	signal<int> update;
    AVFormatContext* file=0;
    AVStream* audioStream=0;
    AVCodecContext* audio=0;
    AudioFormat audioInput,audioOutput;
    int audioPTS=0;
};
