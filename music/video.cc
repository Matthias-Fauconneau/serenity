#include "video.h"
#include "thread.h"
#include "string.h"
#include "graphics.h"

extern "C" {
#define _MATH_H // Prevent system <math.h> inclusion which conflicts with local "math.h"
#define _STDLIB_H // Prevent system <stdlib.h> inclusion which conflicts with local "thread.h"
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h> //avformat
#include <libswscale/swscale.h> //swscale
#include <libavcodec/avcodec.h> //avcodec
#include <libavutil/avutil.h> //avutil
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
}

//void __attribute((constructor(1001))) initialize_FFmpeg() { av_register_all(); } // ffmpeg.cc

Decoder::Decoder(string path) {
	file = avformat_alloc_context();
	file->probesize = File(path).size();
	if(avformat_open_input(&file, strz(path), 0, 0)) { log("No such file", path); return; }
	file->max_analyze_duration = 	AV_TIME_BASE; // 1 sec
	avformat_find_stream_info(file, 0);
	if(file->duration <= 0) { file=0; log("Invalid file"); return; }
	for(uint i=0; i<file->nb_streams; i++) {
		if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
			videoStream = file->streams[i];
			video = videoStream->codec;
			AVCodec* codec = avcodec_find_decoder(video->codec_id);
			if(codec && avcodec_open2(video, codec, 0) >= 0) {
				width = video->width; height=video->height;
				assert_(videoStream->time_base.den%videoStream->time_base.num == 0);
				videoFrameRate = video->time_base.den/videoStream->time_base.num;
			}
		}
	}
	//if(video->pix_fmt==-1) video->pix_fmt=AV_PIX_FMT_YUV422P;
	assert_(video->pix_fmt!=-1);
	assert_(video->pix_fmt == AV_PIX_FMT_YUVJ422P);
	swsContext = sws_getContext (width, height, video->pix_fmt, width, height,  AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, 0, 0, 0);
	assert_(swsContext);
	frame = av_frame_alloc();
}

Decoder::~Decoder() {
	if(frame) av_frame_free(&frame);
	if(file) avformat_close_input(&file);
}

Image Decoder::read() {
	for(;;) {
		AVPacket packet;
		if(av_read_frame(file, &packet) < 0) return {};
		if(file->streams[packet.stream_index]==videoStream) {
			int gotFrame=0;
			int used = avcodec_decode_video2(video, frame, &gotFrame, &packet);
			assert_(used >= 0, used);
			if(gotFrame) {
				AVPicture targetFrame;
				Image image (size);
				targetFrame.data[0] = ((uint8*)image.data);
				targetFrame.data[1] = ((uint8*)image.data)+1;
				targetFrame.data[2] = ((uint8*)image.data)+2;
				targetFrame.linesize[0] = targetFrame.linesize[1] = targetFrame.linesize[2] = image.stride*4;
				sws_scale(swsContext, frame->data, frame->linesize, 0, height, targetFrame.data, targetFrame.linesize);

				//if(!firstPTS) firstPTS=frame->pkt_pts; // Ignores any embedded sync
				assert_(frame->pkt_pts >= firstPTS, firstPTS, packet.pts, packet.dts);
				videoTime = (frame->pkt_pts-firstPTS) * videoFrameRate * videoStream->time_base.num / videoStream->time_base.den;
				return image;
			}
		}
		av_free_packet(&packet);
	}
}

void Decoder::seek(uint64 videoTime) {
	assert_(videoStream->time_base.num == 1);
	av_seek_frame(file, videoStream->index, videoTime*videoStream->time_base.den/videoFrameRate, 0);
	this->videoTime = videoTime; // FIXME: actual
}
