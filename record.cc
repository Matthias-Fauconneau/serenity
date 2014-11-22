#include <linux/videodev2.h>
#include "asound.h"
#include "window.h"
#include "encoder.h"
#include "interface.h"

typedef IOR<'V', 0, v4l2_capability> QueryCapabilities;
typedef IOWR<'V', 4, v4l2_format> GetFormat;
typedef IOWR<'V', 8, v4l2_requestbuffers> RequestBuffers;
typedef IOWR<'V', 9, v4l2_buffer> QueryBuffer;
typedef IOWR<'V', 15, v4l2_buffer> QueueBuffer;
typedef IOWR<'V', 17, v4l2_buffer> DequeueBuffer;
typedef IOW<'V', 18, int> StartStream;

struct VideoInput : Device, Poll {
	array<Map> buffers;
	int2 size;
	size_t videoTime = 0;
	function<void(YUYVImage)> write;
	VideoInput(function<void(YUYVImage)> write) : Device("/dev/video0"), Poll(Device::fd,POLLIN), write(write) {
        v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE}; iowr<GetFormat>(fmt);
		size = int2(fmt.fmt.pix.width, fmt.fmt.pix.height);
		assert_(fmt.fmt.pix.bytesperline==uint(2*size.x));
        const int bufferSize = 4;
        v4l2_requestbuffers req = {.count = bufferSize, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
		iowr<RequestBuffers>(req);
        assert_(req.count == bufferSize);
        for(uint bufferIndex: range(req.count)) {
			v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = bufferIndex};
            iowr<QueryBuffer>(buf);
            assert_(buf.length == fmt.fmt.pix.height*fmt.fmt.pix.bytesperline);
            buffers.append(Map(Device::fd, buf.m.offset, buf.length, Map::Prot(Map::Read|Map::Write)));
            iowr<QueueBuffer>(buf);
        }
        iow<StartStream>(V4L2_BUF_TYPE_VIDEO_CAPTURE);
    }
	void event() {
        v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
		iowr<DequeueBuffer>(buf);
		assert_(buf.sequence == videoTime, videoTime, buf.sequence);
		assert_(buf.bytesused == uint(size.y*size.x*2));
		write(YUYVImage(cast<byte2>(buffers[buf.index]), size));
		videoTime++;
		iowr<QueueBuffer>(buf);
    }
};

struct Record : ImageView {
	Encoder encoder {arguments()[0]+".mkv"_};
    AudioInput audio {{this, &Record::write16}, {this, &Record::write32}, 2, 96000, 8192};
    VideoInput video {{this, &Record::write}};
    Window window {this, video.size, [](){return "Record"__;}};
	Record() { // -> Encoder
		encoder.setVideo(Encoder::YUYV, video.size, 30/*FIXME*/);
		encoder.setFLAC(audio.sampleBits, 1, audio.rate);
		encoder.open();
        assert_(audio.periodSize == encoder.audioFrameSize, audio.periodSize, encoder.audioFrameSize, encoder.channels, encoder.audioFrameRate);
		if(audio.sampleBits==16) log("16bit audio capture");
		image = Image(video.size);
		window.show();
	}
	void write(YUYVImage image) {
		encoder.writeVideoFrame(image);
		for(size_t i: range(image.ref<byte2>::size/2)) {
			int U = int(image[2*i+0][1])-128;
			int V = int(image[2*i+1][1])-128;
			int b = (int(2.018*(1<<16)) * U) >> 16;
			int g = - ((int(0.391*(1<<16)) * U) >> 16) - ((int(0.813*(1<<16)) * V) >> 16);
			int r = (int(1.596*(1<<16)) * V) >> 16;
			for(size_t j: range(2)) { //TODO: fixed point
				int Y = image[2*i+j][0];
				int y = (int(1.164*(1<<16)) * (Y-16)) >> 16;
				int B = y + b;
				int G = y + g;
				int R = y + r;
				this->image[2*i+j] = byte4(clip(0, B, 0xFF), clip(0, G, 0xFF), clip(0, R, 0xFF),0xFF);
			}
		}
		window.render();
	}
	uint write16(ref<int16> input) { // -> Encoder
		if(audio.channels == encoder.channels) encoder.writeAudioFrame(input);
		else {
			assert_(audio.channels==2 && encoder.channels==1);
			buffer<int16> mono (input.size/audio.channels);
			for(size_t i: range(mono.size)) mono[i] = (input[i*2+0] + input[i*2+1]) >> 17; // Assumes 1bit headroom
			assert_(mono.size <= encoder.audioFrameSize);
			encoder.writeAudioFrame(mono);
		}
		return input.size;
	}
	uint write32(ref<int32> input) { // -> Encoder
		// Downmix
		assert_(audio.channels==2 && encoder.channels==1);
		buffer<int32> mono (input.size/audio.channels);
		for(size_t i: range(mono.size)) mono[i] = (input[i*2+0] + input[i*2+1]) / 2; // Assumes 1bit headroom
		assert_(mono.size <= encoder.audioFrameSize);
		encoder.writeAudioFrame(mono);
		return input.size;
	}
} app;
