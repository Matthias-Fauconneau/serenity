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

__attribute((constructor(1001))) void initialize_FFmpeg() { av_register_all(); avformat_network_init(); } // ffmpeg.cc

Decoder::Decoder(string path) {
 //file = avformat_alloc_context();
 //file->probesize = File(path).size();
 if(avformat_open_input(&file, strz(path), 0, 0)) { log("No such file", path); return; }
 avformat_find_stream_info(file, 0);
 if(file->duration <= 0) { file=0; log("Invalid file"); return; }
 for(uint i=0; i<file->nb_streams; i++) {
  if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
   videoStream = file->streams[i];
   videoCodec = videoStream->codec;
   AVCodec* codec = avcodec_find_decoder(videoCodec->codec_id);
   if(codec && avcodec_open2(videoCodec, codec, 0) >= 0) {
    width = videoCodec->width; height=videoCodec->height;
    //assert_(videoCodec->time_base.num == 1, videoCodec->time_base.num, videoCodec->time_base.den);
    //videoFrameRate = videoCodec->time_base.den/videoCodec->time_base.num;
    //assert_(videoCodec->time_base.den == videoFrameRate, videoCodec->time_base.den);
    //assert_(videoStream->duration != AV_NOPTS_VALUE);
    //timeNum = videoCodec->time_base.num;
    timeDen = videoStream->time_base.den;
    duration = videoStream->duration; //*videoStream->time_base.num/videoStream->time_base.den;
    //duration = file->duration*videoFrameRate/AV_TIME_BASE;
   }
  }
 }
 //if(videoCodec->pix_fmt==-1) videoCodec->pix_fmt=AV_PIX_FMT_YUV422P;
 assert_(videoCodec->pix_fmt!=-1);
 frame = av_frame_alloc();
}

Decoder::~Decoder() {
 if(frame) av_frame_free(&frame);
 if(swsContext) sws_freeContext(swsContext);
 if(file) avformat_close_input(&file);
}

bool Decoder::read(const Image& image) {
 assert_(file);
 //assert_(image.size == size, image.size, size);
 for(;;) {
  AVPacket packet = {};
  av_init_packet(&packet); packet.data=0, packet.size=0;
  if(av_read_frame(file, &packet) < 0) { av_free_packet(&packet); return false; }
  if(file->streams[packet.stream_index]==videoStream) {
   int gotFrame=0;
   int used = avcodec_decode_video2(videoCodec, frame, &gotFrame, &packet);
   assert_(used >= 0);
   if(gotFrame) {
    if(image) scale(image);
    //if(!firstPTS) firstPTS=frame->pkt_pts; // Ignores any embedded sync
    //assert_(frame->pkt_pts >= firstPTS, firstPTS, packet.pts, packet.dts);
    int nextTime = frame->pkt_pts; // * videoFrameRate * videoStream->time_base.num / videoStream->time_base.den;
    //assert_(nextTime == videoTime+1, videoTime, nextTime, videoFrameRate, frame->pkt_pts);
    videoTime = nextTime;
    av_free_packet(&packet);
    return true;
   }
  }
  av_free_packet(&packet);
 }
}
Image Decoder::read() { Image image(size); if(read(image)) return image; else return {}; }

Image8 Decoder::Y() const {
 assert_(frame->linesize[0] == frame->width);
 return Image8(buffer<uint8>(frame->data[0], frame->linesize[0]*height, 0), int2(frame->width, frame->height), frame->linesize[0]);
}

void Decoder::scale(const Image& image) {
 if(image.size != scaledSize) {
  if(swsContext) sws_freeContext(swsContext);
  swsContext = sws_getContext(width, height, videoCodec->pix_fmt == AV_PIX_FMT_YUVJ422P ? PIX_FMT_YUV422P : videoCodec->pix_fmt,
                              image.width, image.height,  AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR, 0, 0, 0);
  assert_(swsContext);
  scaledSize = image.size;
 }
 AVPicture targetFrame;
 targetFrame.data[0] = ((uint8*)image.data);
 targetFrame.data[1] = ((uint8*)image.data)+1;
 targetFrame.data[2] = ((uint8*)image.data)+2;
 targetFrame.linesize[0] = targetFrame.linesize[1] = targetFrame.linesize[2] = image.stride*4;
 sws_scale(swsContext, frame->data, frame->linesize, 0, height, targetFrame.data, targetFrame.linesize);
}
Image Decoder::scale() { Image image(size); scale(image); return image; }


void Decoder::seek(uint64 videoTime) {
 //assert_(videoStream->time_base.num == 1);
 av_seek_frame(file, videoStream->index, videoTime/**videoStream->time_base.den/videoFrameRate*/, 0);
 //this->videoTime = videoTime; // FIXME: actual
}
