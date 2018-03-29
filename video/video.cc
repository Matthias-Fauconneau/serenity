#include "video.h"
#include "time.h"
extern "C" {
#include <libavformat/avformat.h> // avformat avcodec avutil
#include <libswscale/swscale.h> // swscale
}

__attribute((constructor(1001))) static void initialize_FFmpeg() { av_register_all(); avformat_network_init(); } // ffmpeg.cc

Decoder::Decoder(string path) {
    file = avformat_alloc_context();
    file->probesize = File(path).size();
    if(avformat_open_input(&file, strz(path), nullptr, nullptr)) { log("No such file"_, path); return; }
    avformat_find_stream_info(file, nullptr);
    for(AVStream* stream: ref<AVStream*>(file->streams, file->nb_streams)) {
        if(stream->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {
            videoStream = stream;
            size.x = stream->codecpar->width; size.y = stream->codecpar->height;
            videoTimeNum = stream->time_base.num;
            videoTimeDen = stream->time_base.den;
            duration = stream->duration; //*videoStream->time_base.num/videoStream->time_base.den;
            AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            videoCodec = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(videoCodec, stream->codecpar);
            check( avcodec_open2(videoCodec, nullptr, nullptr) );
        }
    }
    assert_(videoCodec->pix_fmt!=-1);
    frame = av_frame_alloc();
}

Decoder::~Decoder() {
    if(frame) av_frame_free(&frame);
    if(swsContext) sws_freeContext(swsContext);
    if(videoCodec) avcodec_free_context(&videoCodec);
    if(file) avformat_close_input(&file);
}

bool Decoder::read(const Image& image) {
    assert_(file);
    for(;;) {
        if(avcodec_receive_frame(videoCodec, frame) == 0) {
            if(image) {
                if(videoCodec->pix_fmt == AV_PIX_FMT_RGB24) {
                    for(size_t y: range(size.y)) for(size_t x: range(size.x)) {
                        image(x,y).r = frame->data[0][y*frame->linesize[0]+x*3+0];
                        image(x,y).g = frame->data[0][y*frame->linesize[0]+x*3+1];
                        image(x,y).b = frame->data[0][y*frame->linesize[0]+x*3+2];
                        image(x,y).a = 0xFF;
                    }
                } else if(videoCodec->pix_fmt == AV_PIX_FMT_GBRP) {
                    for(size_t y: range(size.y)) for(size_t x: range(size.x)) {
                        image(x,y).g = frame->data[0][y*frame->linesize[0]+x];
                        image(x,y).b = frame->data[1][y*frame->linesize[1]+x];
                        image(x,y).r = frame->data[2][y*frame->linesize[2]+x];
                        image(x,y).a = 0xFF;
                    }
                } else if(videoCodec->pix_fmt == AV_PIX_FMT_YUV420P) { // FIXME: luminance only // FIXME: no copy
                    for(size_t y: range(size.y)) for(size_t x: range(size.x)) {
                        image(x,y).g = frame->data[0][y*frame->linesize[0]+x];
                        image(x,y).b = frame->data[0][y*frame->linesize[0]+x];
                        image(x,y).r = frame->data[0][y*frame->linesize[0]+x];
                        image(x,y).a = 0xFF;
                    }
                } else error(int(videoCodec->pix_fmt));
            }
            videoTime = frame->pts;
            return true;
        }
        AVPacket packet; av_init_packet(&packet); packet.data=nullptr; packet.size=0;
        if(av_read_frame(file, &packet) < 0) { av_packet_unref(&packet); return false; }
        if(file->streams[packet.stream_index]==videoStream) avcodec_send_packet(videoCodec, &packet);
        av_packet_unref(&packet);
    }
}

Image8 Decoder::YUV(size_t i) const {
 assert_(frame->linesize[i] == (i ? frame->width/2 : frame->width));
 return Image8(buffer<uint8>(frame->data[i], frame->linesize[i]*(size.y/(i?2:1)), 0), uint2(frame->width/(i?2:1), frame->height/(i?2:1)), frame->linesize[i]);
}

void Decoder::seek(uint64 videoTime) { av_seek_frame(file, videoStream->index, videoTime, 0); }
