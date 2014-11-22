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
typedef IOWR<'V', 75, v4l2_frmivalenum> EnumerateFrameIntervals;

struct VideoInput : Device, Poll {
	array<Map> buffers;
	int2 size;
    uint frameRate = 0;
	size_t videoTime = 0;
    uint64 firstTimeStamp = 0;
    uint64 timeStampDen = 1000000;
    function<void(YUYVImage&&)> write;
    VideoInput(function<void(YUYVImage&&)> write, Thread& thread=mainThread) : Device("/dev/video0"), Poll(Device::fd,POLLIN,thread), write(write) {
        v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE}; iowr<GetFormat>(fmt);
		size = int2(fmt.fmt.pix.width, fmt.fmt.pix.height);
		assert_(fmt.fmt.pix.bytesperline==uint(2*size.x));
        v4l2_frmivalenum frmi = {.pixel_format=V4L2_PIX_FMT_YUYV, .width=uint(size.x), .height=uint(size.y)};
        iowr<EnumerateFrameIntervals>(frmi);
        assert_(frmi.type==V4L2_FRMIVAL_TYPE_DISCRETE && frmi.discrete.numerator == 1);
        frameRate = frmi.discrete.denominator;
        assert_(frameRate == 30);
        const int bufferedFrameCount = 1;
        v4l2_requestbuffers req = {.count = bufferedFrameCount, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
		iowr<RequestBuffers>(req);
        assert_(req.count == bufferedFrameCount);
        for(uint bufferIndex: range(req.count)) {
			v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = bufferIndex};
            iowr<QueryBuffer>(buf);
            assert_(buf.length == fmt.fmt.pix.height*fmt.fmt.pix.bytesperline);
            buffers.append(Map(Device::fd, buf.m.offset, buf.length, Map::Prot(Map::Read|Map::Write)));
            iowr<QueueBuffer>(buf);
        }
    }
    void start() {
        iow<StartStream>(V4L2_BUF_TYPE_VIDEO_CAPTURE);
    }
	void event() {
        v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
		iowr<DequeueBuffer>(buf);
        //assert_(buf.sequence == videoTime, videoTime, buf.sequence);
        if(buf.sequence != videoTime) {
            log(videoTime, buf.sequence);
            videoTime = buf.sequence;
        }
        assert_(buf.bytesused == uint(size.y*size.x*2));
        uint64 timeStamp = buf.timestamp.tv_sec*1000000+buf.timestamp.tv_usec;
        if(!firstTimeStamp) firstTimeStamp = timeStamp;
        write(YUYVImage(unsafeRef(cast<byte2>(buffers[buf.index])), size, timeStamp-firstTimeStamp));
		videoTime++;
		iowr<QueueBuffer>(buf);
    }
};

struct Record : ImageView, Poll {
    Lock lock;
    Encoder encoder {arguments()[0]+".mkv"_};

    Thread audioThread;// {-20};
    AudioInput audio {{this, &Record::bufferAudio}, 2, 48000/*96000/192000*/, 4096 /*ChromeOS kernel restricts maximum buffer size*/, audioThread};
    array<buffer<int32>> audioFrames;
    int32 maximumAmplitude = 0;
    int32 reportedMaximumAmplitude = 0;
    int32 clipped = 0, total = 0;
    int32 reportedClipped = 0;

    Thread videoThread;// {-19};
    VideoInput video {{this, &Record::bufferVideo}, videoThread};
    Window window {this, video.size, [](){return "Record"__;}};
    array<YUYVImage> videoFrames;
    YUYVImage lastImage;
    bool contentChanged = false;

    Record() {
        encoder.setVideo(Encoder::YUYV, video.size, video.frameRate);
		encoder.setFLAC(audio.sampleBits, 1, audio.rate);
		encoder.open();
        assert_(audio.periodSize <= encoder.audioFrameSize, audio.periodSize, encoder.audioFrameSize, encoder.channels, encoder.audioFrameRate);
        assert_(audio.sampleBits==32);
		image = Image(video.size);
		window.show();
        audioThread.spawn();
        videoThread.spawn();
        audio.start();
        video.start();
    }
    ~Record() {
        audioThread.wait();
        videoThread.wait();
    }

    uint bufferAudio(ref<int32> input) {
        // Downmix
        assert_(audio.channels==2 && encoder.channels==1);
        buffer<int32> mono (input.size/audio.channels);
        for(size_t i: range(mono.size)) {
            assert(abs(input[i*2+0])<1<<31 && abs(input[i*2+1])<1<<31);
            int s = (input[i*2+0] + input[i*2+1]) / 2; // Assumes 1bit headroom
            if(abs(s) > maximumAmplitude) maximumAmplitude = abs(s);
            mono[i] = s;
        }
        total += mono.size;
        assert_(mono.size <= encoder.audioFrameSize);
        Locker locker(lock);
        audioFrames.append(move(mono));
        queue();
        return input.size/audio.channels;
    }

    void bufferVideo(YUYVImage&& image) {
        Locker locker(lock);
        videoFrames.append(YUYVImage(copyRef(image), image.size, image.time)); //FIXME: merge with deinterleaving to avoid a copy
        queue();
    }

    // Encoder thread (TODO: multithread ?)
    void event() {
        for(;;) {
            lock.lock();
            if(videoFrames) { // 18MB/s
                YUYVImage image = videoFrames.take(0);
                lock.unlock();

                encoder.writeVideoFrame(image);
                lastImage = move(image);
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
        if(maximumAmplitude > reportedMaximumAmplitude) {
            log("Max", maximumAmplitude, log2(real(maximumAmplitude)));
            reportedMaximumAmplitude = maximumAmplitude;
        }
        if(clipped > reportedClipped) {
            log(clipped, total, 100.*clipped/total);
            reportedClipped = clipped;
        }
    }

    shared<Graphics> graphics(int2 size) override {
        if(contentChanged) {
            const int bU = 2.018*(1<<16);
            const int gU = 0.391*(1<<16);
            const int gV = 0.813*(1<<16);
            const int rV = 1.596*(1<<16);
            const int yY = 1.164*(1<<16);
            for(size_t i: range(lastImage.ref<byte2>::size/2)) {
                int U = int(lastImage[2*i+0][1])-128;
                int V = int(lastImage[2*i+1][1])-128;
                int b = (bU * U) >> 16;
                int g = - ((gU * U + gV * V) >> 16);
                int r = (rV * V) >> 16;
                for(size_t j: range(2)) { //TODO: fixed point
                    int Y = lastImage[2*i+j][0];
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
