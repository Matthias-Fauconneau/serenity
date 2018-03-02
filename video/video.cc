#include "video.h"
#include "time.h"
extern "C" {
#include <libavformat/avformat.h> // avformat avcodec avutil
#include <libswscale/swscale.h> // swscale
}

__attribute((constructor(1001))) void initialize_FFmpeg() { av_register_all(); avformat_network_init(); } // ffmpeg.cc

Decoder::Decoder(string path) {
    file = avformat_alloc_context();
    file->probesize = File(path).size();
    if(avformat_open_input(&file, strz(path), 0, 0)) { log("No such file"_, path); return; }
    avformat_find_stream_info(file, 0);
    for(AVStream* stream: ref<AVStream*>(file->streams, file->nb_streams)) {
        if(stream->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
            videoStream = stream;
            videoCodec = videoStream->codec;
            AVCodec* codec = avcodec_find_decoder(videoCodec->codec_id);
            if(codec && avcodec_open2(videoCodec, codec, 0) >= 0) {
                width = videoCodec->width; height=videoCodec->height;
                timeDen = videoStream->time_base.den;
                duration = videoStream->duration; //*videoStream->time_base.num/videoStream->time_base.den;
            }
        }
    }
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
    for(;;) {
        AVPacket packet = {};
        av_init_packet(&packet); packet.data=0, packet.size=0;
        int e;
        if((e = av_read_frame(file, &packet)) < 0) { av_free_packet(&packet); log(e); return false; }
        if(file->streams[packet.stream_index]==videoStream) {
            int gotFrame=0;
            avcodec_decode_video2(videoCodec, frame, &gotFrame, &packet);
            assert_(!videoCodec->refcounted_frames);
            if(gotFrame) {
                if(videoCodec->pix_fmt == AV_PIX_FMT_RGB24) {
                    for(size_t y: range(height)) for(size_t x: range(width)) {
                        image(x,y).r = frame->data[0][y*frame->linesize[0]+x*3+0];
                        image(x,y).g = frame->data[0][y*frame->linesize[0]+x*3+1];
                        image(x,y).b = frame->data[0][y*frame->linesize[0]+x*3+2];
                        image(x,y).a = 0xFF;
                    }
                } else if(videoCodec->pix_fmt == AV_PIX_FMT_GBRP) {
                    for(size_t y: range(height)) for(size_t x: range(width)) {
                        image(x,y).g = frame->data[0][y*frame->linesize[0]+x];
                        image(x,y).b = frame->data[1][y*frame->linesize[1]+x];
                        image(x,y).r = frame->data[2][y*frame->linesize[2]+x];
                        image(x,y).a = 0xFF;
                    }
                } else error(int(videoCodec->pix_fmt));
                int nextTime = frame->pkt_pts;
                videoTime = nextTime;
                av_free_packet(&packet);
                return true;
            }
        }
        av_free_packet(&packet);
    }
}

void Decoder::seek(uint64 videoTime) { av_seek_frame(file, videoStream->index, videoTime, 0); }
