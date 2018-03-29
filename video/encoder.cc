#include "encoder.h"
#include "thread.h"
#include "string.h"
extern "C" {
#include <libavformat/avformat.h> // avformat avcodec avutil
#include <libswscale/swscale.h> // swscale
#include <libavutil/pixdesc.h>
}

#undef check
#define check(expr, args...) ({ auto e = expr; if(int(e)<0) error(#expr ""_, ##args); e; })

Encoder::Encoder(string name) : path(copyRef(name)) {
    //lock.lock();
    av_register_all();
    avformat_alloc_output_context2(&file, nullptr, nullptr, strz(path));
}

void Encoder::setH264(const uint2 size, const uint videoTimeDen, const uint videoTimeNum) {
    assert_(!videoStream);
    this->size=size;
    this->videoTimeNum = videoTimeNum;
    this->videoTimeDen = videoTimeDen;
    swsContext = sws_getContext(size.x, size.y, AV_PIX_FMT_BGR0, size.x, size.y, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    videoCodec = avcodec_alloc_context3(codec);
    assert_(videoCodec->codec_id == codec->id);
    videoCodec->width = size.x;
    videoCodec->height = size.y;
    videoCodec->pix_fmt = AV_PIX_FMT_YUV420P;
    if(file->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = nullptr;
    av_dict_set(&options, "preset", "ultrafast", 0);
    //av_dict_set(&options, "crf", "0", 0);
    //av_dict_set(&options, "x264-params", "lossless=1:bframes=0", 0);
    videoCodec->time_base.num = videoTimeNum;
    videoCodec->time_base.den = videoTimeDen;
    check( avcodec_open2(videoCodec, nullptr, &options) );

    videoStream = avformat_new_stream(file, codec);
    videoStream->time_base.num = videoCodec->time_base.num;
    videoStream->time_base.den = videoCodec->time_base.den;
    avcodec_parameters_from_context(videoStream->codecpar, videoCodec);

    frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = size.x;
    frame->height = size.y;
    av_frame_get_buffer(frame, 0);
}

#ifdef H265
void Encoder::setH265(uint2 size, uint videoFrameRate) {
    assert_(!videoStream);
    this->size=size;
    //swsContext = sws_getContext(size.x, size.y, AV_PIX_FMT_BGR0, size.x, size.y, AV_PIX_FMT_GBRP, SWS_FAST_BILINEAR, 0, 0, 0);
    swsContext = sws_getContext(size.x, size.y, AV_PIX_FMT_BGR0, size.x, size.y, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, 0, 0, 0);

    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H265);
    videoStream = avformat_new_stream(context, codec);
    videoCodec = videoStream->codec;
    avcodec_get_context_defaults3(videoCodec, codec);
    videoCodec->codec_id = codec->id;
    videoCodec->width = size.x;
    videoCodec->height = size.y;
    videoStream->time_base.num = videoCodec->time_base.num = 1;
    videoStream->time_base.den = videoCodec->time_base.den = videoFrameRate;
    this->videoFrameRateNum = videoFrameRate;
    this->videoFrameRateDen = 1;
    videoCodec->pix_fmt = AV_PIX_FMT_YUV420P; //AV_PIX_FMT_GBRP; //AV_PIX_FMT_BGR0;
    if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = 0;
    av_dict_set(&options, "preset", "ultrafast", 0);
    //av_dict_set(&options, "crf", "0", 0);
    //av_dict_set(&options, "x265-params", "lossless=1:bframes=0", 0);
    check( avcodec_open2(videoCodec, codec, &options) );
    frame = av_frame_alloc();
    avpicture_alloc((AVPicture*)frame, AV_PIX_FMT_YUV420P/*AV_PIX_FMT_GBRP*//*AV_PIX_FMT_BGR0*/, size.x, size.y);
    frame->format = AV_PIX_FMT_YUV420P; //AV_PIX_FMT_GBRP; //AV_PIX_FMT_BGR0;
    frame->width = size.x;
    frame->height = size.y;
}
#endif

#ifdef APNG
void Encoder::setAPNG(uint2 size, uint videoFrameRate) {
    assert_(!videoStream);
    this->size=size;
    swsContext = sws_getContext(size.x, size.y, AV_PIX_FMT_BGR0, size.x, size.y, AV_PIX_FMT_RGB24, 0, 0, 0, 0);

    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_APNG);
    videoStream = avformat_new_stream(context, codec);
    videoCodec = videoStream->codec;
    avcodec_get_context_defaults3(videoCodec, codec);
    videoCodec->codec_id = codec->id;
    videoCodec->width = size.x;
    videoCodec->height = size.y;
    videoStream->time_base.num = videoCodec->time_base.num = 1;
    videoStream->time_base.den = videoCodec->time_base.den = videoFrameRate;
    this->videoFrameRateNum = videoFrameRate;
    this->videoFrameRateDen = 1;
    videoCodec->pix_fmt = AV_PIX_FMT_RGB24;
    if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = 0;
    av_dict_set(&options, "plays", "0", 0);
    check( avcodec_open2(videoCodec, codec, &options) );
    frame = av_frame_alloc();
    avpicture_alloc((AVPicture*)frame, AV_PIX_FMT_RGB24, size.x, size.y);
    frame->format = AV_PIX_FMT_RGB24;
    frame->width = size.x;
    frame->height = size.y;
}
#endif

void Encoder::open() {
    assert_(file && (videoStream /*|| audioStream*/));
    avio_open(&file->pb, strz(path), AVIO_FLAG_WRITE);
    AVDictionary* options = nullptr;
    assert_(videoStream);
    av_dict_set(&options, "movflags", "faststart", 0);
    check( avformat_write_header(file, &options) );
    //lock.unlock();
}

void Encoder::writeVideoFrame(const Image& image, int64 pts) {
    assert_(videoStream && image.size==size, image.size);
    assert_(swsContext);
    int stride = image.stride*4;
    const uint8* const imageData = reinterpret_cast<const uint8* const>(image.data);
    sws_scale(swsContext, &imageData, &stride, 0, size.y, frame->data, frame->linesize);
    frame->pts = pts;
    avcodec_send_frame(videoCodec, frame);
    writeFrame(videoCodec, videoStream);
}

void Encoder::writeFrame(AVCodecContext* context, const AVStream* stream) {
    AVPacket pkt; av_init_packet(&pkt); pkt.data=nullptr; pkt.size=0;
    while(avcodec_receive_packet(context, &pkt) == 0) {
        pkt.stream_index = stream->index;
        av_interleaved_write_frame(file, &pkt);
    }
}

Encoder::~Encoder() {
    if(frame) av_frame_free(&frame);
    if(videoCodec) {
        avcodec_send_frame(videoCodec, nullptr);
        writeFrame(videoCodec, videoStream);
        avcodec_free_context(&videoCodec);
    }
    av_interleaved_write_frame(file, nullptr);
    av_write_trailer(file);
    avio_close(file->pb);
    avformat_free_context(file);
    file = nullptr;
}
