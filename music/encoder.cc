#include "encoder.h"
#include "thread.h"
#include "string.h"
#include "graphics.h"

extern "C" {
#define _MATH_H // Prevent system <math.h> inclusion which conflicts with local "math.h"
#define _STDLIB_H // Prevent system <stdlib.h> inclusion which conflicts with local "thread.h"
#define __STDC_CONSTANT_MACROS
#include <stdint.h> //lzma
#include <libavformat/avformat.h> //avformat
#include <libswscale/swscale.h> //swscale
#include <libavcodec/avcodec.h> //avcodec
#include <libavcodec/avcodec.h> //va
#include <libavutil/avutil.h> //avutil
#include <libavutil/opt.h> //fdk-aac
#include <libavutil/channel_layout.h> //x264
#include <libavutil/mathematics.h> //swresample
}

#undef check
#define check(expr, args...) ({ auto e = expr; if(int(e)<0) error(#expr ""_, (const char*)av_err2str(int(e)), ##args); e; })

Encoder::Encoder(String&& name) : path(move(name)) {
	av_register_all();
	if(existsFile(path)) remove(strz(path));
	avformat_alloc_output_context2(&context, NULL, NULL, strz(path));
}

void Encoder::setVideo(Format format, int2 size, uint videoFrameRate) {
	assert_(!videoStream);
	this->size=size;
	this->videoFrameRate=videoFrameRate;
	swsContext = sws_getContext(width, height, format==sRGB ? AV_PIX_FMT_BGRA : AV_PIX_FMT_YUYV422, width, height,
								format==sRGB ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV422P, SWS_FAST_BILINEAR, 0, 0, 0);

	AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	videoStream = avformat_new_stream(context, codec);
	videoCodec = videoStream->codec;
	avcodec_get_context_defaults3(videoCodec, codec);
	videoCodec->codec_id = AV_CODEC_ID_H264;
	videoCodec->width = width;
	videoCodec->height = height;
    videoStream->time_base.num = 1;
    videoStream->time_base.den = videoFrameRate;
	videoCodec->pix_fmt = format==sRGB ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV422P;
	if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
	check( avcodec_open2(videoCodec, codec, 0) );
	frame = av_frame_alloc();
	avpicture_alloc((AVPicture*)frame, format==sRGB ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV422P, width, height);
}

void Encoder::setAudio(const AudioFile& audio) {
	assert_(!audioStream);
	audioFrameRate = audio.audioFrameRate;
	AVCodec* codec = avcodec_find_encoder(audio.audio->codec_id);
	audioStream = avformat_new_stream(context, codec);
	audioCodec = audioStream->codec;
    if(context->oformat->flags & AVFMT_GLOBALHEADER) audioCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    check( avcodec_copy_context(audioCodec, audio.audio) );
	audioCodec->codec_tag = 0;
}

void Encoder::setAAC(uint channels, uint rate) {
	assert_(!audioStream);
	this->channels = channels;
	audioFrameRate = rate;
	AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	audioStream = avformat_new_stream(context, codec);
	audioStream->id = 1;
	audioCodec = audioStream->codec;
	audioCodec->codec_id = AV_CODEC_ID_AAC;
	audioCodec->sample_fmt  = AV_SAMPLE_FMT_S16;
	audioCodec->bit_rate = 192000;
	audioCodec->sample_rate = rate;
	audioCodec->channels = channels;
	audioCodec->channel_layout = ref<int>{0,AV_CH_LAYOUT_MONO,AV_CH_LAYOUT_STEREO}[channels];
    if(context->oformat->flags & AVFMT_GLOBALHEADER) audioCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    avcodec_open2(audioCodec, codec, 0);
	audioFrameSize = audioCodec->frame_size;
}

void Encoder::setFLAC(uint sampleBits, uint channels, uint rate) {
	assert_(!audioStream);
	this->channels = channels;
	audioFrameRate = rate;
	AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
	audioStream = avformat_new_stream(context, codec);
	audioStream->id = 1;
	audioCodec = audioStream->codec;
	audioCodec->codec_id = AV_CODEC_ID_FLAC;
	assert_(sampleBits==16 || sampleBits==32);
	audioCodec->sample_fmt  = sampleBits==16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32;
	audioCodec->sample_rate = rate;
	audioCodec->channels = channels;
	audioCodec->channel_layout = ref<int>{0,AV_CH_LAYOUT_MONO,AV_CH_LAYOUT_STEREO}[channels];
    if(context->oformat->flags & AVFMT_GLOBALHEADER) audioCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
	avcodec_open2(audioCodec, codec, 0);
	audioFrameSize = audioCodec->frame_size;
}

void Encoder::open() {
	assert_(context && (videoStream || audioStream));
	avio_open(&context->pb, strz(path), AVIO_FLAG_WRITE);
	check( avformat_write_header(context, 0) );
}

/*void Encoder::writeVideoFrame(YUYVImage image) {
	assert_(videoStream && image.size==int2(width,height), image.size);
	AVFrame* framePtr = av_frame_alloc(); AVFrame& frame = *framePtr;
	frame.format = AV_PIX_FMT_YUYV422;
	frame.width = image.width;
	frame.height = image.height;
	frame.data[0] = (uint8*)image.data;
	frame.linesize[0] = image.width*2;
	frame.pts = videoTime*videoStream->time_base.den/(videoFrameRate*videoStream->time_base.num);

	AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
	int gotVideoPacket;
	avcodec_encode_video2(videoCodec, &pkt, &frame, &gotVideoPacket);
	avpicture_free((AVPicture*)&frame);
	av_frame_free(&framePtr);
	if(gotVideoPacket) {
		pkt.stream_index = videoStream->index;
		av_interleaved_write_frame(context, &pkt);
		videoEncodedTime++;
	}

	videoTime++;
}*/

void Encoder::writeVideoFrame(YUYVImage image) {
	assert_(videoStream && image.size==int2(width,height), image.size);
	int stride = image.width * 2;
	sws_scale(swsContext, &(uint8*&)image.data, &stride, 0, height, frame->data, frame->linesize);
	frame->pts = videoTime*videoStream->time_base.den/(videoFrameRate*videoStream->time_base.num);

	AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
	int gotVideoPacket;
	avcodec_encode_video2(videoCodec, &pkt, frame, &gotVideoPacket);

	if(gotVideoPacket) {
		pkt.stream_index = videoStream->index;
		av_interleaved_write_frame(context, &pkt);
		videoEncodedTime++;
	}
	videoTime++;
}

void Encoder::writeVideoFrame(const Image& image) {
	assert_(videoStream && image.size==int2(width,height), image.size);
	int stride = image.stride*4;
	sws_scale(swsContext, &(uint8*&)image.data, &stride, 0, height, frame->data, frame->linesize);
	frame->pts = videoTime*videoStream->time_base.den/(videoFrameRate*videoStream->time_base.num);

    AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
    int gotVideoPacket;
	avcodec_encode_video2(videoCodec, &pkt, frame, &gotVideoPacket);
    if(gotVideoPacket) {
        pkt.stream_index = videoStream->index;
        av_interleaved_write_frame(context, &pkt);
        videoEncodedTime++;
    }
    videoTime++;
}

void Encoder::writeAudioFrame(ref<int16> audio) {
	assert_(audioStream && audioCodec->sample_fmt==AV_SAMPLE_FMT_S16);
	AVFrame frame;
	frame.nb_samples = audio.size/channels;
	avcodec_fill_audio_frame(&frame, channels, AV_SAMPLE_FMT_S16, (uint8*)audio.data, audio.size * channels * sizeof(int16), 1);
	frame.pts = audioTime*audioStream->time_base.den/(audioFrameRate*audioStream->time_base.num);

	AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
	int gotAudioPacket;
	avcodec_encode_audio2(audioCodec, &pkt, &frame, &gotAudioPacket);
	if(gotAudioPacket) {
		pkt.stream_index = audioStream->index;
		av_interleaved_write_frame(context, &pkt);
		audioEncodedTime += audio.size/channels;
	}

	audioTime += audio.size/channels;
}

void Encoder::writeAudioFrame(ref<int32> audio) {
	assert_(audioStream && audioCodec->sample_fmt==AV_SAMPLE_FMT_S32);
	AVFrame frame;
	frame.nb_samples = audio.size/channels;
	assert_(frame.nb_samples < audioCodec->frame_size);
	avcodec_fill_audio_frame(&frame, channels, AV_SAMPLE_FMT_S32, (uint8*)audio.data, audio.size * channels * sizeof(int32), 1);
	frame.pts = audioTime*audioStream->time_base.den/(audioFrameRate*audioStream->time_base.num);

	AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
	int gotAudioPacket;
	avcodec_encode_audio2(audioCodec, &pkt, &frame, &gotAudioPacket);
	if(gotAudioPacket) {
		pkt.stream_index = audioStream->index;
		av_interleaved_write_frame(context, &pkt);
		audioEncodedTime += audio.size/channels;
	}

	audioTime += audio.size/channels;
}

Encoder::~Encoder() {
	assert_(context);
	if(videoStream) for(;;) { // FIXME: flush audio
        AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
        int gotVideoPacket = 0;
        avcodec_encode_video2(videoCodec, &pkt, 0, &gotVideoPacket);
        if(!gotVideoPacket) break;
        pkt.pts = av_rescale_q(videoEncodedTime, videoCodec->time_base, videoStream->time_base);
        if (videoCodec->coded_frame->key_frame) pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index = videoStream->index;
        av_interleaved_write_frame(context, &pkt);
        videoEncodedTime++;
	}
    av_interleaved_write_frame(context, 0);
    av_write_trailer(context);
    avformat_free_context(context); context=0;
}
