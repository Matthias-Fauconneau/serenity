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
    VideoInput(function<void(YUYVImage)> write, Thread& thread=mainThread) : Device("/dev/video0"), Poll(Device::fd,POLLIN,thread), write(write) {
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

struct Record : ImageView, Poll {
    Lock lock;
    Encoder encoder {arguments()[0]+".mkv"_};

    Thread audioThread;// {-20};
    AudioInput audio {{this, &Record::bufferAudio}, 1, 96000, 8192, audioThread};
    array<buffer<int32>> audioFrames;

    Thread videoThread;// {-19};
    VideoInput video {{this, &Record::bufferVideo}, videoThread};
    Window window {this, video.size, [](){return "Record"__;}};
    array<buffer<byte2>> videoFrames;
    buffer<byte2> lastFrame;
    bool contentChanged = false;

    Record() {
		encoder.setVideo(Encoder::YUYV, video.size, 30/*FIXME*/);
		encoder.setFLAC(audio.sampleBits, 1, audio.rate);
		encoder.open();
        assert_(audio.periodSize == encoder.audioFrameSize, audio.periodSize, encoder.audioFrameSize, encoder.channels, encoder.audioFrameRate);
		if(audio.sampleBits==16) log("16bit audio capture");
		image = Image(video.size);
		window.show();
	}

    uint bufferAudio(ref<int32> input) {
        // Downmix
        assert_(audio.channels==2 && encoder.channels==1);
        buffer<int32> mono (input.size/audio.channels);
        for(size_t i: range(mono.size)) mono[i] = (input[i*2+0] + input[i*2+1]) / 2; // Assumes 1bit headroom
        assert_(mono.size <= encoder.audioFrameSize);
        Locker locker(lock);
        audioFrames.append(move(mono));
        queue();
        return input.size;
    }

    void bufferVideo(YUYVImage image) {
        Locker locker(lock);
        videoFrames.append(copyRef(image)); //FIXME: merge with deinterleaving to avoid a copy
        queue();
    }

    // Encoder thread (TODO: multithread ?)
    void event() {
        for(;;) {
            lock.lock();
            if(videoFrames) { // 18MB/s
                buffer<byte2> frame = videoFrames.take(0);
                lock.unlock();

                YUYVImage image(frame, video.size);
                encoder.writeVideoFrame(image);
                lastFrame = move(frame);
                contentChanged = true;

                continue;
            }
            if(audioFrames) { // 1MB/s
                buffer<int32> frame = audioFrames.take(0);
                lock.unlock();

                encoder.writeAudioFrame(frame);

                continue;
            }
            lock.unlock();
            break;
        }
        if(contentChanged) window.render(); // only when otherwise idle
    }

    shared<Graphics> graphics(int2 size) override {
        if(contentChanged) {
            const int bU = 2.018*(1<<16);
            const int gU = 0.391*(1<<16);
            const int gV = 0.813*(1<<16);
            const int rV = 1.596*(1<<16);
            const int yY = 1.164*(1<<16);
            for(size_t i: range(lastFrame.size/2)) {
                int U = int(lastFrame[2*i+0][1])-128;
                int V = int(lastFrame[2*i+1][1])-128;
                int b = (bU * U) >> 16;
                int g = - ((gU * U + gV * V) >> 16);
                int r = (rV * V) >> 16;
                for(size_t j: range(2)) { //TODO: fixed point
                    int Y = lastFrame[2*i+j][0];
                    int y = (yY * (Y-16)) >> 16;
                    int B = y + b;
                    int G = y + g;
                    int R = y + r;
                    this->image[2*i+j] = byte4(clip(0, B, 0xFF), clip(0, G, 0xFF), clip(0, R, 0xFF),0xFF);
                }
            }
            contentChanged = false;
        }
        return ImageView::graphics(size);
    }
} app;
