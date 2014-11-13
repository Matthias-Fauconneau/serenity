#include "decoder.h"
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
	if(avformat_open_input(&file, strz(path), 0, 0)) { log("No such file", path); return; }
	//file->flags |= /*AVFMT_FLAG_IGNDTS|*/AVFMT_FLAG_SORT_DTS;
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
		if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
			audioStream = file->streams[i];
			audio = audioStream->codec;
			audio->request_sample_fmt = AV_SAMPLE_FMT_S16;
			AVCodec* codec = avcodec_find_decoder(audio->codec_id);
			if(codec && avcodec_open2(audio, codec, 0) >= 0) {
				rate = audio->sample_rate;
				duration = audioStream->duration*rate*audioStream->time_base.num/audioStream->time_base.den;
			}
		}
	}
	assert(audio && audio->sample_rate && (uint)audio->channels == channels
		   && audio->sample_fmt == AV_SAMPLE_FMT_S16);
	swsContext = sws_getContext (width, height, video->pix_fmt, width, height,  AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, 0, 0, 0);
	assert_(swsContext);
}

Decoder::~Decoder() {
	if(frame) av_frame_free(&frame);
	if(file) avformat_close_input(&file);
}

void Decoder::read(const Image& image) {
	for(;;) {
		AVPacket packet;
		if(av_read_frame(file, &packet) < 0) return;
		if(file->streams[packet.stream_index]==videoStream) {
			if(!frame) frame = av_frame_alloc(); int gotFrame=0;
			int used = avcodec_decode_video2(video, frame, &gotFrame, &packet);
			assert_(used >= 0, used);
			if(!gotFrame) continue;

			AVPicture targetFrame;
			targetFrame.data[0] = ((uint8*)image.data);
			targetFrame.data[1] = ((uint8*)image.data)+1;
			targetFrame.data[2] = ((uint8*)image.data)+2;
			targetFrame.linesize[0] = targetFrame.linesize[1] = targetFrame.linesize[2] = image.stride*4;
			sws_scale(swsContext, frame->data, frame->linesize, 0, height, targetFrame.data, targetFrame.linesize);

			//if(!firstPTS) firstPTS=frame->pkt_pts; // Ignores any embedded sync
			assert_(frame->pkt_pts >= firstPTS, firstPTS, packet.pts, packet.dts);
			videoTime = (frame->pkt_pts-firstPTS) * videoFrameRate * videoStream->time_base.num / videoStream->time_base.den;
			return;
		}
		av_free_packet(&packet);
	}
}

/*size_t Decoder::read(mref<short2> output) {
	uint readSize = 0;
	while(readSize<output.size) {
		if(!bufferSize) {
			AVPacket packet;
			if(av_read_frame(file, &packet) < 0) return readSize;
			if(file->streams[packet.stream_index]==audioStream) {
				shortBuffer = buffer<short2>();
				if(!frame) frame = av_frame_alloc(); int gotFrame=0;
				int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
				if(used < 0 || !gotFrame) continue;
				bufferIndex=0, bufferSize = frame->nb_samples;
				if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
					shortBuffer = unsafeRef(ref<short2>((short2*)frame->data[0], bufferSize)); // Valid until next frame
				}
				else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
					shortBuffer = buffer<short2>(bufferSize);
					for(uint i : range(bufferSize)) for(uint j : range(2)) {
						int s = ((float*)frame->data[j])[i]*(1<<14); //TODO: ReplayGain
						if(s<-(1<<15) || s >= (1<<15)) error("Clip", s, ((float*)frame->data[j])[i]);
						shortBuffer[i][j] = s;
					}
				}
				else error("Unimplemented conversion to int16 from", (int)audio->sample_fmt);
				audioTime = packet.pts*audioStream->time_base.num*rate/audioStream->time_base.den;
			}
			av_free_packet(&packet);
		}
		uint size = min(bufferSize, output.size-readSize);
		output.slice(readSize, size).copy(shortBuffer.slice(bufferIndex, size));
		bufferSize -= size; bufferIndex += size; readSize += size;
	}
	assert(readSize == output.size);
	return readSize;
}*/
