#include "record.h"
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
//#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

void Record::start(const string& name, bool video, bool audio) {
    av_register_all();
    if(context) stop();

    String path = homePath()+"/"_+name+".mp4"_;
    if(existsFile(path)) remove(strz(path));
    struct AVFormatContext* context=0;
    avformat_alloc_output_context2(&context, 0, 0, strz(path));

    if(video) { // Video
        AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        videoStream = avformat_new_stream(context, codec);
        videoStream->id = 0;
        videoCodec = videoStream->codec;
        avcodec_get_context_defaults3(videoCodec, codec);
        videoCodec->codec_id = AV_CODEC_ID_H264;
        videoCodec->bit_rate = 3000000;
        videoCodec->width = width;
        videoCodec->height = height;
        videoStream->time_base.num = videoCodec->time_base.num = 1;
        videoStream->time_base.den = videoCodec->time_base.den = fps;
        videoCodec->pix_fmt = PIX_FMT_YUV420P;
        if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        AVDictionary* options=0;
        av_dict_set(&options, "preset","ultrafast",0);
        avcodec_open2(videoCodec, codec, &options);
    }

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

    swsContext = sws_getContext(width, height, PIX_FMT_BGR0, width, height, PIX_FMT_YUV420P, SWS_FAST_BILINEAR, 0, 0, 0);

    videoTime = 0, videoEncodedTime = 0, audioTime = 0;
    this->context = context; // Set once completely ready (in case methods are called in other threads while starting)
}

void Record::captureVideoFrame() {
    if(!videoStream) return;
    AVFrame* frame = avcodec_alloc_frame();
    {Locker lock(framebufferLock);
        const Image& image = framebuffer;
        assert_(image);
        if(width!=image.width || height!=image.height) { log("Window size",image.width,"x",image.height,"changed while recording at", width,"x", height); return; }
        avpicture_alloc((AVPicture*)frame, PIX_FMT_YUV420P, width, height);
        int stride = image.stride*4; sws_scale(swsContext, &(uint8*&)image.data, &stride, 0, height, frame->data, frame->linesize);
    }
    frame->pts = videoTime*videoStream->time_base.den/(fps*videoStream->time_base.num);

    AVPacket pkt = {}; av_init_packet(&pkt);
    int gotVideoPacket;
    avcodec_encode_video2(videoCodec, &pkt, frame, &gotVideoPacket);
    avpicture_free((AVPicture*)frame);
    avcodec_free_frame(&frame);
    if(gotVideoPacket) {
        if (videoCodec->coded_frame->key_frame) pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index = videoStream->index;
        av_interleaved_write_frame(context, &pkt);
        videoEncodedTime++;
    }

    videoTime++;
}

void Record::captureAudioFrame(float* audio, uint audioSize) {
    if(!audioStream) return;
    AVFrame *frame = avcodec_alloc_frame();
    frame->nb_samples = audioSize;
    if(audioSize != 1024) error("Bad frame size", audioSize);
    buffer<int16> audio16(audioSize*2);
    for(uint i: range(audioSize*2)) {
        float s = audio[i]*0x1p-16;
        if(s < -(1<<15) || s >= (1<<15) ) error(s);
        audio16[i] = s;
    }
    avcodec_fill_audio_frame(frame, 2, AV_SAMPLE_FMT_S16, (uint8*)audio16.data, audioSize * 2 * sizeof(int16), 1);
    frame->pts = audioTime;

    AVPacket pkt={}; av_init_packet(&pkt);
    int gotAudioPacket;
    avcodec_encode_audio2(audioCodec, &pkt, frame, &gotAudioPacket);
    avcodec_free_frame(&frame);
    if (gotAudioPacket) {
        pkt.stream_index = audioStream->index;
        av_interleaved_write_frame(context, &pkt);
    }

    audioTime += audioSize;
}

void Record::capture(float* audio, uint audioSize) {
    if(!context) return;
    assert_(videoStream->time_base.num==1);
    if(videoStream && videoTime*rate <= audioTime*fps) captureVideoFrame();
    if(audioStream) captureAudioFrame(audio,audioSize);
}

void Record::stop() {
    if(!context) return;

    //FIXME: video lags on final frames
    if(videoStream) for(;;) {
        int gotVideoPacket;
        {AVPacket pkt = {}; av_init_packet(&pkt);
            avcodec_encode_video2(videoCodec, &pkt, 0, &gotVideoPacket);
            if(gotVideoPacket) {
                pkt.pts = av_rescale_q(videoEncodedTime, videoCodec->time_base, videoStream->time_base);
                if (videoCodec->coded_frame->key_frame) pkt.flags |= AV_PKT_FLAG_KEY;
                pkt.stream_index = videoStream->index;
                av_interleaved_write_frame(context, &pkt);
                videoEncodedTime++;
            }
        }

        if(!gotVideoPacket) break;
    }

    if(audioStream) for(;;) {
        int gotAudioPacket;
        {AVPacket pkt={}; av_init_packet(&pkt);
            avcodec_encode_audio2(audioCodec, &pkt, 0, &gotAudioPacket);
            if(gotAudioPacket) {
                pkt.stream_index = audioStream->index;
                av_interleaved_write_frame(context, &pkt);
            }
        }

        if(!gotAudioPacket) break;
    }

    av_write_trailer(context);
    avformat_free_context(context); context=0;
}
