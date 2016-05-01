#include "audio.h"
#include "string.h"

/// Generic audio decoder (using FFmpeg)
extern "C" {
#define _MATH_H // Prevent system <math.h> inclusion which conflicts with local "math.h"
#define _STDLIB_H // Prevent system <stdlib.h> inclusion which conflicts with local "thread.h"
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h> //avformat
#include <libavcodec/avcodec.h> //avcodec
#include <libavutil/avutil.h> //avutil
}

__attribute((constructor(1001))) void initialize_FFmpeg() { av_register_all(); }

FFmpeg::FFmpeg(string path) {
 if(avformat_open_input(&file, strz(path), 0, 0)) { log("No such file"_, path); return; }
 avformat_find_stream_info(file, 0);
 if(file->duration <= 0) { file=0; log("Invalid file"); return; }
 frame = av_frame_alloc();
 for(uint i=0; i<file->nb_streams; i++) {
  if(file->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
   audioStream = file->streams[i];
   audio = audioStream->codec;
   audio->request_sample_fmt = AV_SAMPLE_FMT_S16;
   AVCodec* codec = avcodec_find_decoder(audio->codec_id);
   if(codec && avcodec_open2(audio, codec, 0) >= 0) {
    this->codec = Codec(ref<AVCodecID>{AV_CODEC_ID_AAC, AV_CODEC_ID_FLAC, AV_CODEC_ID_MP3}.indexOf(audio->codec_id));
    channels = audio->channels;
    assert_(channels == 1 || channels == 2);
    audioFrameRate = audio->sample_rate;
    assert_(audioStream->time_base.num == 1, audioStream->time_base.den, audioFrameRate);
    if(audioStream->duration != AV_NOPTS_VALUE) {
     assert_(audioStream->duration != AV_NOPTS_VALUE);
     duration = (int64)audioStream->duration*audioFrameRate*audioStream->time_base.num/audioStream->time_base.den;
    } else {
     duration = (int64)file->duration*audioFrameRate/AV_TIME_BASE;
    }
    assert_(duration);
    break;
   }
  }
 }
 assert_(audio && audio->sample_rate && (uint)audio->channels == channels &&
         (audio->sample_fmt == AV_SAMPLE_FMT_S16 || audio->sample_fmt == AV_SAMPLE_FMT_S16P ||
          audio->sample_fmt == AV_SAMPLE_FMT_FLTP || audio->sample_fmt == AV_SAMPLE_FMT_S32));
}

size_t FFmpeg::read16(mref<int16> output) {
 size_t readSize = 0;
 while(readSize*channels<output.size) {
  if(!bufferSize) {
   AVPacket packet;
   if(av_read_frame(file, &packet) < 0) return readSize;
   if(file->streams[packet.stream_index]==audioStream && packet.pts >= 0 /*FIXME*/) {
    int16Buffer = buffer<int16>();
    int gotFrame=0;
    int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
    if(used < 0 || !gotFrame) continue;
    bufferIndex=0, bufferSize = frame->nb_samples;
    if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
     int16Buffer = unsafeRef(ref<int16>((int16*)frame->data[0], bufferSize*channels)); // Valid until next frame
    }
    else {
     int16Buffer = buffer<int16>(bufferSize*channels);
     if(audio->sample_fmt == AV_SAMPLE_FMT_S32) {
      for(size_t i : range(bufferSize*channels)) int16Buffer[i] = ((int32*)frame->data[0])[i] >> 16;
     } else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
      int min=0, max=0;
      for(size_t i : range(bufferSize)) for(size_t j : range(channels)) {
       int32 s = ((float*)frame->data[j])[i]*22639; //TODO: ReplayGain
       if(s<-(1<<15) || s >= 1<<15) min=::min(min, s), max=::max(max, s);
       int16Buffer[i*channels+j] = s;
      }
      if(min != max) log("Clip", min, max, 22639*32767/::max(-min, max));
     }
     else error("Unimplemented conversion to int16 from", (int)audio->sample_fmt);
    }
    audioTime = packet.pts*audioFrameRate*audioStream->time_base.num/audioStream->time_base.den;
   }
   av_free_packet(&packet);
  }
  size_t size = min(bufferSize, output.size/channels-readSize);
  output.slice(readSize*channels, size*channels).copy(int16Buffer.slice(bufferIndex*channels, size*channels));
  bufferSize -= size; bufferIndex += size; readSize += size;
 }
 //assert(readSize*channels == output.size, readSize, channels, readSize*channels, output.size);
 return readSize;
}

size_t FFmpeg::read32(mref<int32> output) {
 size_t readSize = 0;
 while(readSize*channels < output.size) {
  while(!bufferSize) {
   AVPacket packet;
   if(av_read_frame(file, &packet) < 0) return readSize;
   if(file->streams[packet.stream_index]==audioStream && packet.pts >= 0 /*FIXME*/) {
    int32Buffer = buffer<int32>();
    int gotFrame=0;
    int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
    if(used < 0 || !gotFrame) continue;
    bufferIndex=0, bufferSize = frame->nb_samples;
    assert_(bufferSize);
    if(audio->sample_fmt == AV_SAMPLE_FMT_S32) {
     int32Buffer = unsafeRef(ref<int32>((int32*)frame->data[0], bufferSize*channels)); // Valid until next frame
    }
    else {
     int32Buffer = buffer<int32>(bufferSize*channels);
     if(audio->sample_fmt == AV_SAMPLE_FMT_S16)
      for(size_t i : range(bufferSize*channels)) int32Buffer[i] = ((int16*)frame->data[0])[i] << 16;
     else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
      for(size_t i : range(bufferSize)) for(size_t j : range(channels)) {
       int32 s = ((float*)frame->data[j])[i]*(1<<30); //TODO: ReplayGain
       //if(s<-(1<<31) || s >= int(uint(1<<31)-1)) error("Clip", -(1<<31), s, 1<<31, ((float*)frame->data[j])[i]);
       int32Buffer[i*channels+j] = s;
      }
     }
     else error("Unimplemented conversion to int32 from", (int)audio->sample_fmt);
    }
    audioTime = packet.pts*audioFrameRate*audioStream->time_base.num/audioStream->time_base.den;
   }
   av_free_packet(&packet);
  }
  assert_(bufferSize && readSize*channels < output.size, bufferSize, readSize*channels, output.size);
  size_t size = min(bufferSize, output.size/channels-readSize);
  output.slice(readSize*channels, size*channels).copy(int32Buffer.slice(bufferIndex*channels, size*channels));
  bufferSize -= size; bufferIndex += size; readSize += size;
 }
 //assert(readSize*channels == output.size, readSize, channels, readSize*channels, output.size);
 return readSize;
}

size_t FFmpeg::read(mref<float> output) {
 assert_(channels);
 size_t readSize = 0;
 while(readSize*channels < output.size) {
  while(!bufferSize) {
   AVPacket packet;
   if(av_read_frame(file, &packet) < 0) return readSize;
   if(file->streams[packet.stream_index]==audioStream && packet.pts >= 0 /*FIXME*/) {
    if(!frame) frame = av_frame_alloc(); int gotFrame=0;
    int used = avcodec_decode_audio4(audio, frame, &gotFrame, &packet);
    if(used < 0 || !gotFrame) continue;
    bufferIndex=0, bufferSize = frame->nb_samples;
    floatBuffer = buffer<float>(bufferSize*channels);
    if(audio->sample_fmt == AV_SAMPLE_FMT_S32) {
     for(size_t i : range(bufferSize*channels)) floatBuffer[i] = ((int32*)frame->data[0])[i]*0x1.0p-31;
    }
    else if(audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
     for(size_t i : range(bufferSize)) for(uint c : range(channels)) floatBuffer[i*channels+c] = ((float*)frame->data[c])[i];
    }
    else if(audio->sample_fmt == AV_SAMPLE_FMT_S16) {
     for(size_t i : range(bufferSize*channels)) floatBuffer[i] = ((int16*)frame->data[0])[i]*0x1.0p-15;
    }
    else error("Unimplemented conversion to float32 from", (int)audio->sample_fmt);
    audioTime = packet.pts*audioFrameRate*audioStream->time_base.num/audioStream->time_base.den;
   }
   av_free_packet(&packet);
  }
  assert_(bufferSize && readSize*channels < output.size, bufferSize, readSize*channels, output.size);
  size_t size = min(bufferSize, output.size-readSize*channels);
  output.slice(readSize*channels, size*channels).copy(floatBuffer.slice(bufferIndex*channels, size*channels));
  bufferSize -= size; bufferIndex += size; readSize += size;
 }
 //assert(readSize*channels == output.size, readSize, channels, readSize*channels, output.size);
 return readSize;
}

void FFmpeg::seek(uint audioTime) {
 assert_(audioStream->time_base.num == 1);
 av_seek_frame(file, audioStream->index, (uint64)audioTime*audioStream->time_base.den/audioFrameRate, 0);
 int16Buffer=buffer<int16>(); int32Buffer=buffer<int32>(); floatBuffer=buffer<float>(); bufferIndex=0, bufferSize=0;
 this->audioTime = audioTime; // FIXME: actual
}

FFmpeg::~FFmpeg() {
 if(frame) av_frame_free(&frame);
 duration=0; audioStream = 0; audio=0;
 int16Buffer=buffer<int16>(); int32Buffer=buffer<int32>(); floatBuffer=buffer<float>(); bufferIndex=0, bufferSize=0;
 if(file) avformat_close_input(&file);
}

Audio decodeAudio(string path) {
 FFmpeg file(path);
 Audio audio (buffer<float>(file.duration*file.channels), file.channels, file.audioFrameRate);
 audio.size = file.read(audio) * file.channels;
 return audio;
}
