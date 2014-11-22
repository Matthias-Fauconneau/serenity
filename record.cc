#include <linux/videodev2.h>
#include "asound.h"
#include "encoder.h"

typedef IOR<'V', 0, v4l2_capability> QUERYCAP;
typedef IOR<'V', 4, v4l2_format> G_FMT;
typedef IOWR<'V', 8, v4l2_requestbuffers> REQBUFS;
typedef IOWR<'V', 9, v4l2_buffer> QUERYBUF;
typedef IOWR<'V', 15, v4l2_buffer> QBUF;
typedef IOWR<'V', 17, v4l2_buffer> DQBUF;
typedef IOW<'V', 18, int> STREAMON;


struct VideoInput : Device {
	array<Map> buffers;
	v4l2_format fmt;
	size_t videoTime = 0;
	function<void(YUYVImage)> write;
	VideoInput(function<void(YUYVImage)> write) : Device("/dev/video0"), write(write) {
		v4l2_capability cap = ior<QUERYCAP>();
		assert_(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE &&cap.capabilities & V4L2_CAP_STREAMING);
		//v4l2_cropcap cropcap = {};cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; ioctl(fd, VIDIOC_CROPCAP, &cropcap))
		//v4l2_crop crop;//crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;crop.c = cropcap.defrect;ioctl(fd, VIDIOC_S_CROP, &crop))
		/*v4l2_format{.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
						.pix={.width= 640, .height = 480, .pixelformat = V4L2_PIX_FMT_YUYV, .field = V4L2_FIELD_INTERLACED};
						ioctl(fd, VIDIOC_S_FMT, &fmt))*/
		fmt = ior<G_FMT>();
		v4l2_requestbuffers req = {.count = 4, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .reserved={}};
		iowr<REQBUFS>(req);
		assert_(req.count == 4, req.count);
		for(uint bufferIndex: range(req.count)) {
			v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = bufferIndex};
			iowr<QUERYBUF>(buf);
			buffers.append(Map(Device::fd, buf.m.offset, buf.length, Map::Prot(Map::Read|Map::Write)));
			iowr<QBUF>(buf);
			//iow<STREAMON>(V4L2_BUF_TYPE_VIDEO_CAPTURE);
		}
	}
	void event() {
		v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
		iowr<DQBUF>(buf);
		assert_(buf.sequence == videoTime, videoTime, buf.sequence);
		assert_(buf.bytesused >= fmt.fmt.pix.height*fmt.fmt.pix.bytesperline);
		write(YUYVImage{{.size=int2(fmt.fmt.pix.width, fmt.fmt.pix.height)}, fmt.fmt.pix.bytesperline, buffers[buf.index].slice(0, buf.bytesused)});
		videoTime++;
		iowr<QBUF>(buf);
	}
};

struct Record {
	Encoder encoder {arguments()[0]+".mkv"_};
	AudioInput audio {{this, &Record::write16}, {this, &Record::write32}, 2, 96000, 576};
	VideoInput video {{&encoder, &Encoder::writeVideoFrame}};
	Record() { // -> Encoder
		encoder.setVideo(Encoder::YUYV, int2(1280,720), 60);
		encoder.setFLAC(audio.sampleBits, 1, audio.rate);
		encoder.open();
		assert_(audio.periodSize <= encoder.audioFrameSize, audio.periodSize, encoder.audioFrameSize, encoder.channels, encoder.audioFrameRate);
		if(audio.sampleBits==16) log("16bit audio capture");
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
