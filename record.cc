#include <linux/videodev2.h>
#include "asound.h"
#include "window.h"
#include "encoder.h"
#include "interface.h"
#include "text.h"

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
        const int bufferedFrameCount = 4;
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
            log("Dropped", buf.sequence-videoTime, "frame"+((buf.sequence-videoTime)>1?"s"_:""_));
            videoTime = buf.sequence;
        }
        assert_(buf.bytesused == uint(size.y*size.x*2));
        uint64 timeStamp = buf.timestamp.tv_sec*1000000+buf.timestamp.tv_usec;
        write(YUYVImage(unsafeRef(cast<byte2>(buffers[buf.index])), size, timeStamp));
		videoTime++;
		iowr<QueueBuffer>(buf);
    }
};

struct Record : ImageView, Poll {
    Lock lock;
    Encoder encoder {arguments()[0]+".mkv"_};

    Thread audioThread {-20};
    AudioInput audio {{this, &Record::bufferAudio}, 2, 48000, 4096 /*ChromeOS kernel restricts maximum buffer size*/, audioThread};
    array<buffer<int32>> audioFrames;
    int32 maximumAmplitude = 0;
    int32 reportedMaximumAmplitude = 1<<30;
    float2 smoothedMean = 0;

    Thread videoThread {-19};
    VideoInput video {{this, &Record::bufferVideo}, videoThread};
    array<YUYVImage> videoFrames;
    uint64 firstTimeStamp = 0;

    const int margin = 16;
    Window window {this, video.size+int2(2*margin,margin), [](){return "Record"__;}};
    YUYVImage lastImage;
    Text sizeText, availableText;
    bool contentChanged = false;

    Record() {
        assert_(audio.sampleBits==32);

		image = Image(video.size);
        window.background = Window::NoBackground;
        window.actions[F3] = {this, &Record::restart};
        window.show();

        encoder.setVideo(Encoder::YUYV, video.size, video.frameRate);
        encoder.setFLAC(audio.sampleBits, audio.channels, audio.rate);
        encoder.open();
        assert_(audio.periodSize <= encoder.audioFrameSize);

        audioThread.spawn();
        videoThread.spawn();
        audio.start();
        video.start();
    }
    ~Record() {
        audioThread.wait();
        videoThread.wait();
    }

    void restart() {
        Locker locker(lock);
        audioFrames.clear();
        videoFrames.clear();
        firstTimeStamp = 0;
        encoder.~Encoder();
        new(&encoder) Encoder(arguments()[0]+".mkv"_);
        encoder.setVideo(Encoder::YUYV, video.size, video.frameRate);
        encoder.setFLAC(audio.sampleBits, audio.channels, audio.rate);
        encoder.open();
        assert_(audio.periodSize <= encoder.audioFrameSize);
    }

    uint bufferAudio(ref<int32> input) {
        {Locker locker(lock);
            audioFrames.append(copyRef(input));}
        queue();
        return input.size/audio.channels;
    }

    void bufferVideo(YUYVImage&& image) {
        {Locker locker(lock);
            videoFrames.append(YUYVImage(copyRef(image), image.size, image.time));} //FIXME: merge with deinterleaving to avoid a copy
        queue();
    }

    // Encoder thread (TODO: multithread ?)
    void event() {
        for(;;) {
            lock.lock();
            if(videoFrames) { // 18MB/s
                YUYVImage image = videoFrames.take(0);
                lock.unlock();

                if(!firstTimeStamp) firstTimeStamp = image.time;
                image.time -= firstTimeStamp;
                encoder.writeVideoFrame(image);
                lastImage = move(image);
                contentChanged = true;

                continue;
            }
            if(audioFrames) { // 1MB/s
                buffer<int32> frame = audioFrames.take(0);
                lock.unlock();

                encoder.writeAudioFrame(frame);
                { // Evaluates mean sound levels
                    assert_(encoder.channels == 2);
                    const float a = float(frame.size) / float(audio.rate);
                    maximumAmplitude = (1-a) * maximumAmplitude; // Smoothly recoil maximum back
                    long2 sum;
                    for(int2 s: cast<int2>(frame)) {
                        sum += abs(long2(s));
                        if(encoder.audioTime > encoder.audioFrameRate) { // Amplitude is still setting in first frames
                            if(abs(s[0]) > maximumAmplitude) maximumAmplitude = abs(s[0]);
                            if(abs(s[1]) > maximumAmplitude) maximumAmplitude = abs(s[1]);
                        }
                    }
                    float2 mean = float2(sum / long2(frame.size));
                    smoothedMean = a * mean + (1-a) * smoothedMean;
                }

                continue;
            }
            lock.unlock();
            break;
        }
        if(contentChanged) window.render(); // only when otherwise idle
        if(maximumAmplitude > reportedMaximumAmplitude) {
            float bit = log2(real(maximumAmplitude));
            log("Warning: Measured maximum amplitude at",bit,"bit, leaving only ",31-bit, "headroom");
            reportedMaximumAmplitude = maximumAmplitude;
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
                    image[2*i+j] = byte4(clip(0, B, 0xFF), clip(0, G, 0xFF), clip(0, R, 0xFF),0xFF);
                }
            }
            contentChanged = false;
        }
        shared<Graphics> graphics;
        int2 offset = int2((size.x-image.size.x)/2, size.y-image.size.y);
        { // Capacity meter
            int64 fileSize = File(encoder.path, currentWorkingDirectory()).size();
            int64 available = ::available(encoder.path, currentWorkingDirectory());
            float x = int64(size.x-2*offset.x) * fileSize / (fileSize+available);
            graphics->fills.append(vec2(offset.x, 0), vec2(offset.x + x, offset.y), green);
            graphics->fills.append(vec2(offset.x + x, 0), vec2(size.x-offset.x - (offset.x + x), offset.y), black);
            int fileLengthSeconds = (encoder.videoTime+encoder.videoFrameRate-1)/encoder.videoFrameRate;
            int64 fileSizeMB = (fileSize+1023) / 1024 / 1024;
            sizeText = Text(str(fileLengthSeconds)+"s "+str(fileSizeMB)+"MB", offset.y, white); // FIXME: skip if no change
            graphics->graphics.insertMulti(vec2(offset.x, 0), sizeText.graphics(int2(x, offset.y)));
            int availableLengthMinutes = int64(fileLengthSeconds) * available / fileSize / 60;
            int64 availableMB = available / 1024 / 1024;
            availableText = Text(str(availableLengthMinutes)+"min "+str(availableMB)+"MB", offset.y, white); // FIXME: skip if no change
            graphics->graphics.insertMulti(vec2(offset.x + x, 0), availableText.graphics(int2(size.x-offset.x-(offset.x+x), offset.y)));
        }
        for(int channel : range(audio.channels)) {// VU meters
            float y = size.y * (1 - smoothedMean[channel] / 0x1p28f); //clip(1.f, float(maximumAmplitude), 0x1p28f));
            graphics->fills.append(vec2(channel?offset.x+image.size.x:0, 0), vec2(offset.x, y), black);
            graphics->fills.append(vec2(channel?offset.x+image.size.x:0, y), vec2(offset.x, size.y-y), maximumAmplitude < ((1<<31)-1) ? green : red);
        }
        graphics->graphics.insert(vec2(offset), ImageView::graphics(image.size));
        return graphics;
    }
} app;
