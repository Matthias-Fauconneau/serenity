#include "record.h"
#include "process.h"
#include "string.h"
#include "display.h"

void Record::start(const ref<byte>& name) {
    if(record) return;

    av_register_all();

    string path = getenv("HOME"_)+"/"_+name+".mp4"_;
    avformat_alloc_output_context2(&context, 0, 0, strz(path));

    { // Video
        AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        videoStream = avformat_new_stream(context, codec);
        videoStream->id = 0;
        videoCodec = videoStream->codec;
        avcodec_get_context_defaults3(videoCodec, codec);
        videoCodec->codec_id = AV_CODEC_ID_H264;
        videoCodec->bit_rate = 3000000;
        videoCodec->width = width;
        videoCodec->height = height;
        videoCodec->time_base.num = 1;
        videoCodec->time_base.den = fps;
        videoCodec->pix_fmt = PIX_FMT_YUV420P;
        if(context->oformat->flags & AVFMT_GLOBALHEADER) videoCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        avcodec_open2(videoCodec, codec, 0);
    }

    { // Audio
        AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        audioStream = avformat_new_stream(context, codec);
        audioStream->id = 1;
        audioCodec = audioStream->codec;
        audioCodec->codec_id = AV_CODEC_ID_AAC;
        audioCodec->sample_fmt  = AV_SAMPLE_FMT_S16;
        audioCodec->bit_rate = 192000;
        audioCodec->sample_rate = 44100;
        audioCodec->channels = 2;
        audioCodec->channel_layout = AV_CH_LAYOUT_STEREO;
        if(context->oformat->flags & AVFMT_GLOBALHEADER) audioCodec->flags |= CODEC_FLAG_GLOBAL_HEADER;
        avcodec_open2(audioCodec, codec, 0);
    }

    avio_open(&context->pb, strz(path), AVIO_FLAG_WRITE);
    avformat_write_header(context, 0);

    swsContext = sws_getContext(width, height, PIX_FMT_BGR0, width, height, PIX_FMT_YUV420P, SWS_FAST_BILINEAR, 0, 0, 0);

    videoTime = 0;
    record = true;
}

void Record::capture(float* audio, uint audioSize) {
    if(!record) return;

    if(videoTime*audioCodec->sample_rate <= audioStream->pts.val*videoStream->time_base.den) {
        const Image& image = framebuffer;
        if(width!=image.width || height!=image.height) {
            log("Window size",image.width,"x",image.height,"changed while recording at", width,"x", height); return;
        }
        int stride = image.stride*4;
        AVFrame* frame = avcodec_alloc_frame();
        avpicture_alloc((AVPicture*)frame, PIX_FMT_YUV420P, videoCodec->width, videoCodec->height);
        //FIXME: encode in main thread
        sws_scale(swsContext, &(uint8*&)image.data, &stride, 0, width, frame->data, frame->linesize);
        frame->pts = videoTime;

        AVPacket pkt = {}; av_init_packet(&pkt);
        int gotVideoPacket;
        avcodec_encode_video2(videoCodec, &pkt, frame, &gotVideoPacket);
        av_free(frame->data[0]);
        avcodec_free_frame(&frame);
        if(gotVideoPacket) {
            if (videoCodec->coded_frame->key_frame) pkt.flags |= AV_PKT_FLAG_KEY;
            pkt.stream_index = videoStream->index;
            av_interleaved_write_frame(context, &pkt);
            videoEncodedTime++;
        }

        videoTime++;
    }

    {
        AVFrame *frame = avcodec_alloc_frame();
        frame->nb_samples = audioSize;
        if(audioSize != 1024) error("Bad frame size");
        int16* audio16 = allocate64<int16>(audioSize*2);
        for(uint i: range(audioSize*2)) {
            float s = audio[i]*0x1p-16;
            if(s < -(1<<15) || s >= (1<<15) ) error(s);
            audio16[i] = s;
        }
        avcodec_fill_audio_frame(frame, 2, AV_SAMPLE_FMT_S16, (uint8*)audio16, audioSize * 2 * sizeof(int16), 1);
        frame->pts = audioTime;

        AVPacket pkt={}; av_init_packet(&pkt);
        int gotAudioPacket;
        avcodec_encode_audio2(audioCodec, &pkt, frame, &gotAudioPacket);
        unallocate(audio16);
        avcodec_free_frame(&frame);
        if (gotAudioPacket) {
            pkt.stream_index = audioStream->index;
            av_interleaved_write_frame(context, &pkt);
        }

        audioTime += audioSize;
    }
}

void Record::stop() {
    if(!record) return;
    record = false;

    //FIXME: video lags on final frames
    for(;;) {
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

    for(;;) {
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
    avcodec_close(videoStream->codec);
    avcodec_close(audioStream->codec);
    for(uint i=0; i<context->nb_streams; i++) { av_free(context->streams[i]->codec); av_free(context->streams[i]); }
    avio_close(context->pb);
    av_free(context);
}
