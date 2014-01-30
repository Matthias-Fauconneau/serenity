#include "encoder.h"
#include "thread.h"
#include "string.h"
#include "display.h"

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

Encoder::Encoder(const string& name, bool audio, int width, int height, int fps, int rate)
    : width(width), height(height), fps(fps), rate(rate) {
    av_register_all();
    if(context) stop();

    String path = homePath()+"/"_+name+".mp4"_;
    if(existsFile(path)) remove(strz(path));
    avformat_alloc_output_context2(&context, 0, 0, strz(path));

    // Video
    swsContext = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_YUV444P, SWS_FAST_BILINEAR, 0, 0, 0);
    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    videoStream = avformat_new_stream(context, codec);
    videoStream->id = 0;
    videoCodec = videoStream->codec;
    avcodec_get_context_defaults3(videoCodec, codec);
    videoCodec->codec_id = AV_CODEC_ID_H264;
    videoCodec->width = width;
    videoCodec->height = height;
    videoStream->time_base.num = videoCodec->time_base.num = 1;
    videoStream->time_base.den = videoCodec->time_base.den = fps;
    videoCodec->pix_fmt = AV_PIX_FMT_YUV444P;
    if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options=0;
    av_dict_set(&options, "qp","0",0);
    avcodec_open2(videoCodec, codec, &options);
    assert_(!av_dict_count(options));

    if(audio) { // Audio
        AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        audioStream = avformat_new_stream(context, codec);
        audioStream->id = 1;
        audioCodec = audioStream->codec;
        audioCodec->codec_id = AV_CODEC_ID_AAC;
        audioCodec->sample_fmt  = AV_SAMPLE_FMT_S16;
        audioCodec->bit_rate = 192000;
        audioCodec->sample_rate = rate;
        audioCodec->channels = 2;
        audioCodec->channel_layout = AV_CH_LAYOUT_STEREO;
        if(context->oformat->flags & AVFMT_GLOBALHEADER) audioCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        avcodec_open2(audioCodec, codec, 0);
    }

    avio_open(&context->pb, strz(path), AVIO_FLAG_WRITE);
    avformat_write_header(context, 0);

    assert(swsContext);
}

void Encoder::writeVideoFrame(const Image& image) {
    assert(videoStream && image.size()==int2(width,height));
    ///AVFrame* frame = avcodec_alloc_frame();
    AVFrame frame; avcodec_get_frame_defaults(&frame);
    avpicture_alloc((AVPicture*)&frame, AV_PIX_FMT_YUV444P, width, height);
    int stride = image.stride*4; sws_scale(swsContext, &(uint8*&)image.data, &stride, 0, height, frame.data, frame.linesize);

    frame.pts = videoTime*videoStream->time_base.den/(fps*videoStream->time_base.num);

    AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
    int gotVideoPacket;
#if __x86_64
    setExceptions(Invalid | DivisionByZero | Overflow); // Allows denormal and underflow in x264
#endif
    avcodec_encode_video2(videoCodec, &pkt, &frame, &gotVideoPacket);
#if __x86_64
    setExceptions(Invalid | Denormal | DivisionByZero | Overflow | Underflow);
#endif
    avpicture_free((AVPicture*)&frame);
    if(gotVideoPacket) {
        pkt.stream_index = videoStream->index;
        av_interleaved_write_frame(context, &pkt);
        videoEncodedTime++;
    }

    videoTime++;
    while(audioStream && videoTime*rate >= audioTime*fps) {
        buffer<float2> audio(audioSize);
        audio.size = readAudio(audio);
        writeAudioFrame(audio);
    }
}

void Encoder::writeAudioFrame(const ref<float2>& audio) {
    assert(audioStream && audio.size==audioSize);
    AVFrame frame; avcodec_get_frame_defaults(&frame);
    frame.nb_samples = audio.size;
    avcodec_fill_audio_frame(&frame, 2, AV_SAMPLE_FMT_FLT, (uint8*)audio.data, audio.size * 2 * sizeof(float), 1);
    frame.pts = audioTime;

    AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
    int gotAudioPacket;
    avcodec_encode_audio2(audioCodec, &pkt, &frame, &gotAudioPacket);
    if (gotAudioPacket) {
        pkt.stream_index = audioStream->index;
        av_interleaved_write_frame(context, &pkt);
        audioEncodedTime += audio.size;
    }

    audioTime += audio.size;
}

void Encoder::stop() {
    if(!context) return;

    for(;;) {
        int gotVideoPacket = 0;
        if(videoStream) {
            AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
            avcodec_encode_video2(videoCodec, &pkt, 0, &gotVideoPacket);
            if(gotVideoPacket) {
                pkt.pts = av_rescale_q(videoEncodedTime, videoCodec->time_base, videoStream->time_base);
                if (videoCodec->coded_frame->key_frame) pkt.flags |= AV_PKT_FLAG_KEY;
                pkt.stream_index = videoStream->index;
                av_interleaved_write_frame(context, &pkt);
                videoEncodedTime++;
            }
        }

        int gotAudioPacket = 0;
        if(audioStream) {
            AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
            avcodec_encode_audio2(audioCodec, &pkt, 0, &gotAudioPacket);
            if(gotAudioPacket) {
                pkt.pts = av_rescale_q(audioEncodedTime, audioCodec->time_base, audioStream->time_base);
                pkt.stream_index = audioStream->index;
                av_interleaved_write_frame(context, &pkt);
            }
        }

        if(!gotVideoPacket && !gotAudioPacket) break;
    }

    av_write_trailer(context);
    avformat_free_context(context); context=0;
}
