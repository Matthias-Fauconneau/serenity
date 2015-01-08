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
    lock.lock();
	av_register_all();
	if(existsFile(path)) remove(strz(path));
	avformat_alloc_output_context2(&context, NULL, NULL, strz(path));
}

void Encoder::setMJPEG(int2 size, uint videoFrameRate) {
    assert_(!videoStream);
    this->size=size;
    this->videoFrameRate=videoFrameRate;

    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    videoStream = avformat_new_stream(context, codec);
    videoCodec = videoStream->codec;
    avcodec_get_context_defaults3(videoCodec, codec);
    videoCodec->codec_id = AV_CODEC_ID_MJPEG;
    videoCodec->width = width;
    videoCodec->height = height;
    videoStream->time_base.num = videoCodec->time_base.num = 1;
    videoStream->time_base.den = videoCodec->time_base.den = videoFrameRate;
    videoCodec->pix_fmt = AV_PIX_FMT_YUVJ444P;
    if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    check( avcodec_open2(videoCodec, codec, 0) );
    frame = av_frame_alloc();
}

void Encoder::setH264(int2 size, uint videoFrameRate) {
	assert_(!videoStream);
	this->size=size;
	this->videoFrameRate=videoFrameRate;
    swsContext = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, 0, 0, 0);

	AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	videoStream = avformat_new_stream(context, codec);
	videoCodec = videoStream->codec;
	avcodec_get_context_defaults3(videoCodec, codec);
	videoCodec->codec_id = AV_CODEC_ID_H264;
	videoCodec->width = width;
	videoCodec->height = height;
    videoStream->time_base.num = videoCodec->time_base.num = 1;
    videoStream->time_base.den = videoCodec->time_base.den = videoFrameRate;
    videoCodec->pix_fmt = AV_PIX_FMT_YUV420P;
	videoCodec->max_b_frames = 2; // Youtube constraint
	videoCodec->bit_rate = 5000000;
	if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    check( avcodec_open2(videoCodec, codec, 0) );
	frame = av_frame_alloc();
    avpicture_alloc((AVPicture*)frame, AV_PIX_FMT_YUV420P, width, height);
}

void Encoder::setAudio(const FFmpeg& audio) {
	assert_(!audioStream);
	audioFrameRate = audio.audioFrameRate;
	AVCodec* codec = avcodec_find_encoder(audio.audio->codec_id);
	audioStream = avformat_new_stream(context, codec);
	check( avcodec_copy_context(audioStream->codec, audio.audio) );
	if(context->oformat->flags & AVFMT_GLOBALHEADER) audioStream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
	audioStream->codec->codec_tag = 0;
	// audioCodec is not set (to audioStream->codec) to flag audio stream as being a copy and not encoded (no encoder flush)
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
	audioCodec->sample_rate = rate;
	audioCodec->channels = channels;
	audioCodec->channel_layout = ref<int>{0,AV_CH_LAYOUT_MONO,AV_CH_LAYOUT_STEREO}[channels];
	audioCodec->bit_rate = 192000;
    if(context->oformat->flags & AVFMT_GLOBALHEADER) audioCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    avcodec_open2(audioCodec, codec, 0);
	assert_(audioCodec->bit_rate == 192000, audioCodec->bit_rate);
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
	AVDictionary* options = 0;
	av_dict_set(&options, "movflags", "faststart", 0);
	check( avformat_write_header(context, &options) );
	assert(!options);
    lock.unlock();
}

void Encoder::writeMJPEGPacket(ref<byte> data, int pts) {
    assert_(videoStream);
    frame->pts = pts*videoStream->time_base.den/(1000000*videoStream->time_base.num);

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = (uint8*)data.data;
    pkt.size = data.size;
    pkt.stream_index = videoStream->index;
    Locker locker(lock);
    av_interleaved_write_frame(context, &pkt);
	videoTime++;
}

void Encoder::writeVideoFrame(const Image& image) {
	assert_(videoStream && image.size==int2(width,height), image.size);
	int stride = image.stride*4;
	sws_scale(swsContext, &(uint8*&)image.data, &stride, 0, height, frame->data, frame->linesize);
    //assert_(videoCodec->time_base.num==1 && videoCodec->time_base.den == videoFrameRate);
	frame->pts = videoTime;

    AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
    int gotVideoPacket;
	avcodec_encode_video2(videoCodec, &pkt, frame, &gotVideoPacket);
	if(gotVideoPacket) {
		videoEncodeTime = pkt.dts;
		pkt.dts = pkt.dts*videoStream->time_base.den/(videoFrameRate*videoStream->time_base.num);
		pkt.pts = pkt.pts*videoStream->time_base.den/(videoFrameRate*videoStream->time_base.num);
        pkt.stream_index = videoStream->index;
        Locker locker(lock);
        av_interleaved_write_frame(context, &pkt);
    }
    videoTime++;
}

void Encoder::writeAudioFrame(ref<int16> audio) {
	assert_(audioStream && audioCodec->sample_fmt==AV_SAMPLE_FMT_S16);
	AVFrame frame;
	frame.nb_samples = audio.size/channels;
	avcodec_fill_audio_frame(&frame, channels, AV_SAMPLE_FMT_S16, (uint8*)audio.data, audio.size * channels * sizeof(int16), 1);
	assert_(audioCodec->time_base.num == audioStream->time_base.num);
	assert_(audioCodec->time_base.den == audioStream->time_base.den);
	assert_(audioCodec->time_base.den == int(audioFrameRate));
	frame.pts = audioTime;

	AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
	int gotAudioPacket;
	avcodec_encode_audio2(audioCodec, &pkt, &frame, &gotAudioPacket);
	if(gotAudioPacket) {
		audioEncodeTime = pkt.dts;
		pkt.stream_index = audioStream->index;
        Locker locker(lock);
		av_interleaved_write_frame(context, &pkt);
    }

	audioTime += audio.size/channels;
}

void Encoder::writeAudioFrame(ref<int32> audio) {
	assert_(audioStream && audioCodec->sample_fmt==AV_SAMPLE_FMT_S32);
	AVFrame frame;
	frame.nb_samples = audio.size/channels;
	assert_(frame.nb_samples < audioCodec->frame_size);
	avcodec_fill_audio_frame(&frame, channels, AV_SAMPLE_FMT_S32, (uint8*)audio.data, audio.size * channels * sizeof(int32), 1);
	assert_(audioCodec->time_base.num == audioStream->time_base.num);
	assert_(audioCodec->time_base.den == audioStream->time_base.den);
	assert_(audioCodec->time_base.den == int(audioFrameRate));
	frame.pts = audioTime;

	AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
	int gotAudioPacket;
	avcodec_encode_audio2(audioCodec, &pkt, &frame, &gotAudioPacket);
	if(gotAudioPacket) {
		audioEncodeTime = pkt.dts;
		pkt.stream_index = audioStream->index;
        Locker locker(lock);
		av_interleaved_write_frame(context, &pkt);
    }

	audioTime += audio.size/channels;
}

bool Encoder::copyAudioPacket(FFmpeg& audio) {
    AVPacket packet;
    int status = av_read_frame(audio.file, &packet);
	if(status != 0) { log("copyAudioPacket failed", status); return false; }
    assert_(status == 0);
    if(audio.file->streams[packet.stream_index]==audio.audioStream) {
        assert_(audioStream->time_base.num == audio.audioStream->time_base.num);
        packet.pts=packet.dts=audioTime=
                packet.pts*audioStream->time_base.den/audio.audioStream->time_base.den;
        packet.duration = (int64)packet.duration*audioStream->time_base.den/audio.audioStream->time_base.den;
        packet.stream_index = audioStream->index;
        av_interleaved_write_frame(context, &packet);
    }
	return true;
}

void Encoder::abort() {
    lock.lock();
    if(frame) av_frame_free(&frame);
    if(videoCodec) { avcodec_close(videoCodec); videoCodec=0; videoStream=0; }
    if(audioCodec) { avcodec_close(audioCodec); audioCodec=0; audioStream=0; }
    avio_close(context->pb);
    avformat_free_context(context);
    context = 0;
}

Encoder::~Encoder() {
    if(!context) { assert(!frame && !videoStream && !audioStream && !context); return; } // Aborted
    lock.lock();
    if(frame) av_frame_free(&frame);
	while(videoCodec || audioCodec) {
        AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
        int gotPacket = 0;
		if(videoCodec && (!audioCodec || videoEncodeTime*audioFrameRate<audioEncodeTime*videoFrameRate)) {
            avcodec_encode_video2(videoCodec, &pkt, 0, &gotPacket);
			if(gotPacket) {
				assert_(videoStream);
				pkt.stream_index = videoStream->index;
				videoEncodeTime = pkt.dts;
				pkt.dts = pkt.dts*videoStream->time_base.den/(videoFrameRate*videoStream->time_base.num);
				pkt.pts = pkt.pts*videoStream->time_base.den/(videoFrameRate*videoStream->time_base.num);
			} else {
				avcodec_close(videoCodec); videoCodec=0;
				videoStream=0; //Released by avformat_free_context
				continue;
			}
        }
		if(audioCodec && (!videoCodec || audioEncodeTime*videoFrameRate<=videoEncodeTime*audioFrameRate)) {
            avcodec_encode_audio2(audioCodec, &pkt, 0, &gotPacket);
			if(gotPacket) {
				assert_(audioStream);
				pkt.stream_index = audioStream->index;
				audioEncodeTime = pkt.dts;
				// No time rescale ?
			} else {
				avcodec_close(audioCodec); audioCodec=0;
				audioStream=0; // Released by avformat_free_context
				continue;
			}
        }
		assert_(gotPacket);
        av_interleaved_write_frame(context, &pkt);
        if(!pkt.buf) av_free_packet(&pkt);
    }
	log(videoTime, audioTime, videoEncodeTime, audioEncodeTime);
    av_interleaved_write_frame(context, 0);
    av_write_trailer(context);
    avio_close(context->pb);
    avformat_free_context(context);
    context = 0;
}
