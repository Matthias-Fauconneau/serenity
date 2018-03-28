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
    avformat_alloc_output_context2(&context, nullptr, nullptr, strz(path));
}

void Encoder::setH264(const uint2 size, const uint videoTimeDen, const uint videoTimeNum) {
    assert_(!videoStream);
    this->size=size;
    this->videoTimeNum = videoTimeNum;
    this->videoTimeDen = videoTimeDen;
    swsContext = sws_getContext(size.x, size.y, AV_PIX_FMT_BGR0, size.x, size.y, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    videoStream = avformat_new_stream(context, codec);
    videoCodec = videoStream->codec;
    avcodec_get_context_defaults3(videoCodec, codec);
    videoCodec->codec_id = codec->id;
    videoCodec->width = size.x;
    videoCodec->height = size.y;
    videoStream->time_base.num = videoCodec->time_base.num = videoTimeNum;
    videoStream->time_base.den = videoCodec->time_base.den = videoTimeDen;
    videoCodec->pix_fmt = AV_PIX_FMT_YUV420P;
    if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* options = 0;
    av_dict_set(&options, "preset", "ultrafast", 0);
    //av_dict_set(&options, "crf", "0", 0);
    //av_dict_set(&options, "x264-params", "lossless=1:bframes=0", 0);
    check( avcodec_open2(videoCodec, codec, &options) );
    frame = av_frame_alloc();
    avpicture_alloc((AVPicture*)frame, AV_PIX_FMT_YUV420P, size.x, size.y);
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = size.x;
    frame->height = size.y;
}

#if 0
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

#if APNG
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
    assert_(context && (videoStream /*|| audioStream*/));
    avio_open(&context->pb, strz(path), AVIO_FLAG_WRITE);
    AVDictionary* options = nullptr;
    assert_(videoStream);
    av_dict_set(&options, "movflags", "faststart", 0);
    check( avformat_write_header(context, &options) );
    //lock.unlock();
}

void Encoder::writeVideoFrame(const Image& image) {
    assert_(videoStream && image.size==size, image.size);
    if(swsContext) {
        int stride = image.stride*4;
        const uint8* const imageData = reinterpret_cast<const uint8* const>(image.data);
        sws_scale(swsContext, &imageData, &stride, 0, size.y, frame->data, frame->linesize);
    } else {
        assert_(videoCodec->pix_fmt == AV_PIX_FMT_BGR0);
        error("");
        frame->data[0] = ((uint8*)image.begin())+0;
        frame->data[1] = ((uint8*)image.begin())+1;
        frame->data[2] = ((uint8*)image.begin())+2;
        frame->linesize[0] = frame->linesize[1] = frame->linesize[2] = image.stride*4;
    }
    frame->pts = videoTime;

    AVPacket pkt; av_init_packet(&pkt); pkt.data=nullptr; pkt.size=0;
    int gotVideoPacket;
    avcodec_encode_video2(videoCodec, &pkt, frame, &gotVideoPacket);
    if(gotVideoPacket) {
        videoEncodeTime = pkt.dts;
        //pkt.dts = pkt.dts*videoStream->time_base.den*videoTimeDen/(videoTimeNum*videoStream->time_base.num);
        //pkt.pts = pkt.pts*videoStream->time_base.den*videoTimeDen/(videoTimeNum*videoStream->time_base.num);
        pkt.stream_index = videoStream->index;
        av_interleaved_write_frame(context, &pkt);
    }
    videoTime++;
}

Encoder::~Encoder() {
    if(frame) av_frame_free(&frame);
    while(videoCodec) {
        AVPacket pkt; av_init_packet(&pkt); pkt.data=0, pkt.size=0;
        int gotPacket = 0;
        if(videoCodec) {
            avcodec_encode_video2(videoCodec, &pkt, 0, &gotPacket);
            if(gotPacket) {
                assert_(videoStream);
                pkt.stream_index = videoStream->index;
                videoEncodeTime = pkt.dts;
                //pkt.dts = pkt.dts*videoStream->time_base.den*videoTimeDen/(videoTimeNum*videoStream->time_base.num);
                //pkt.pts = pkt.pts*videoStream->time_base.den*videoTimeDen/(videoTimeNum*videoStream->time_base.num);
            } else {
                avcodec_close(videoCodec); videoCodec=0;
                videoStream=0; //Released by avformat_free_context
                continue;
            }
        }
        assert_(gotPacket);
        av_interleaved_write_frame(context, &pkt);
        if(!pkt.buf) av_free_packet(&pkt);
    }
    log("videoTime", videoTime, "videoEncodeTime", videoEncodeTime);
    av_interleaved_write_frame(context, 0);
    av_write_trailer(context);
    avio_close(context->pb);
    avformat_free_context(context);
    context = 0;
}
