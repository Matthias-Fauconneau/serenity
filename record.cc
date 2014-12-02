#include "asound.h"
#include "video-input.h"
#include "window.h"
#include "encoder.h"
#include "interface.h"
#include "text.h"
#include "jpeg.h"

struct Record : Poll, Widget {
    Lock lock;
    unique<Encoder> encoder = nullptr;

    Thread audioThread {-20};
    AudioInput audio {{this, &Record::bufferAudio}, audioThread};
    array<buffer<int32>> audioFrames;
    int32 maximumAmplitude = 0;
    int32 reportedMaximumAmplitude = 1<<30;
    float smoothedMean = 0;

    Thread videoThread {-20};
    VideoInput video {{this, &Record::bufferVideo}, videoThread};
    struct VideoFrame : buffer<byte> { uint64 time=0; VideoFrame() {} VideoFrame(buffer<byte>&& data, uint64 time) : buffer<byte>(::move(data)), time(time) {} };
    array<VideoFrame> videoFrames;
    uint64 firstTimeStamp = 0;

    const int margin = 16;
    Window window {this, int2(video.width+2*margin,video.height+margin), [](){return "Record"__;}};
    Lock viewLock;
    VideoFrame lastFrame;
    Image image;
    Text sizeText, availableText;
    bool contentChanged = false;

    const bool encodeAudio = !arguments().contains("noaudio");
    const bool encodeVideo = !arguments().contains("novideo");

    // Beeps on overrun
    AudioOutput audioOutput = {{this, &Record::read32}};
    int remainingBeepTime = 0; // in samples (/channels)
    size_t read32(mref<int2> output) {
        for(size_t i: range(output.size)) output[i] = (1<<30)*sin(2*PI*i*440/48000);
        remainingBeepTime -= output.size;
        if(remainingBeepTime <= 0) audioOutput.stop();
        return output.size;
    }

    Record() {
        assert_(audio.sampleBits==32);

        window.background = Window::NoBackground;
        window.actions[F1] = window.actions[LeftArrow] = {this, &Record::abort};
        window.actions[F8] = window.actions[DownArrow] = {this, &Record::stop};
        window.actions[F3] = window.actions[RightArrow] = {this, &Record::start};
        window.actions[Space] = {this, &Record::toggle};
        window.show();

        video.start();
        videoThread.spawn();
        audio.start(2, 48000, 4096 /*ChromeOS kernel restricts maximum buffer size*/);
        audioThread.spawn(); // after registerPoll in audio.setup
    }
    ~Record() {
        audioThread.wait();
        videoThread.wait();
    }

    void start() {
        if(audio) audio.stop();
        abort();
        Locker locker(lock);
        firstTimeStamp = 0;
        encoder = unique<Encoder>(arguments()[0]+".mkv"_);
        if(encodeVideo) encoder->setMJPEG(int2(video.width, video.height), video.frameRate);
        if(encodeAudio) encoder->setFLAC(audio.sampleBits, 1, 48000);
        encoder->open();
        audio.start(2, encoder->audioFrameRate, 4096 /*ChromeOS kernel restricts maximum buffer size*/);
        assert_(audio.periodSize <= encoder->audioFrameSize);
        audioThread.wait(); // Terminated once no poll left after unregisterPoll in audio.stop
        assert_(!audioThread);
        audioThread.spawn();
    }

    /// Stops capture and aborts encoding
    void abort() {
        Locker locker(lock);
        audioFrames.clear();
        videoFrames.clear();
        if(encoder) {
            encoder->abort();
            encoder = nullptr;
        }
    }

    /// Stops capture, flushes buffers and writes up file
    void stop() {
        Locker locker(lock); // Stops capture while encoding buffer
        assert_(encoder);
        while(encode(false)) {}
        assert_(!audioFrames && !videoFrames, audioFrames.size, videoFrames.size);
        encoder = nullptr;
    }

    void toggle() {
        if(!encoder) start();
        else stop();
    }

    uint bufferAudio(ref<int32> input) {
        buffer<int32> mono (input.size/audio.channels);
        for(size_t i: range(input.size/audio.channels)) mono[i] = input[i*2+0]/2 + input[i*2+1]/2; // Assumes 1bit footroom

        if(!encoder || !encodeAudio) evaluateSoundLevel(mono);
        else {
            Locker locker(lock);
            assert_(encoder->channels==1);
            audioFrames.append(move(mono));
        }
        queue();
        return input.size/audio.channels;
    }

    void bufferVideo(ref<byte> data, uint64 time) {
        if(!encoder || !encodeVideo) {
            Locker locker(viewLock);
            lastFrame = {copyRef(data), time}; // FIXME: decompress in same thread to avoid copy
            contentChanged = true;
        } else {
            Locker locker(lock);
            videoFrames.append(copyRef(data), time); // FIXME: record in same thread to avoid copy
        }
        queue();
    }

    // Encoder thread (TODO: multithread ?)
    void event() {
        if(audio.overruns && encoder) {
            remainingBeepTime = 48000;
            audioOutput.start(48000, 4096, 32, 2);
            stop();
            audio.overruns = 0;
            return;
        }
        while(encode()) {}
        if(contentChanged) window.render(); // only when otherwise idle
        if(maximumAmplitude > reportedMaximumAmplitude) {
            float bit = log2(real(maximumAmplitude));
            log("Warning: Measured maximum amplitude at",bit,"bit, leaving only ",31-bit, "headroom");
            reportedMaximumAmplitude = maximumAmplitude;
        }
    }

    bool encode(bool lock=true) {
        if(!encoder) return false;
        if(lock) this->lock.lock();
        if(videoFrames /*&& encoder->videoTime*encoder->audioFrameRate <= (encoder->audioTime+encoder->audioFrameSize)*encoder->videoFrameRate*/) {
            VideoFrame frame = videoFrames.take(0);
            if(lock) this->lock.unlock();

            if(!firstTimeStamp) firstTimeStamp = frame.time;
            encoder->writeMJPEGPacket(frame, frame.time-firstTimeStamp);
            Locker locker(viewLock);
            lastFrame = move(frame);
            contentChanged = true;

            return true;
        }
        if(audioFrames /*&& encoder->audioTime*encoder->videoFrameRate <= (encoder->videoTime+1)*encoder->audioFrameRate*/) {
            buffer<int32> frame = audioFrames.take(0);
            if(lock) this->lock.unlock();

            encoder->writeAudioFrame(frame);
            evaluateSoundLevel(frame);

            return true;
        }
        if(lock) this->lock.unlock();
        return false;
    }

    /// Evaluates mean sound levels
    void evaluateSoundLevel(ref<int32> frame) {
        const float a = float(frame.size) / float(audio.rate);
        maximumAmplitude = (1-a) * maximumAmplitude; // Smoothly recoil maximum back
        int64 sum = 0;
        for(int32 s: frame) {
            sum += abs(int64(s));
            if(audio.time > audio.rate) // Amplitude is still setting in first frames
                maximumAmplitude = max(maximumAmplitude, s);
        }
        int32 mean = sum / frame.size;
        smoothedMean = a * mean + (1-a) * smoothedMean;
        contentChanged = true;
    }

    int2 sizeHint(int2) { return image.size+int2(2*margin, margin); }
    shared<Graphics> graphics(int2 size) override {
        if(contentChanged) {
            Locker locker(viewLock);
            if(lastFrame) image = decodeJPEG(lastFrame);
            contentChanged = false;
        }
        shared<Graphics> graphics;
        int2 offset = int2((size.x-image.size.x)/2, size.y-image.size.y);
        if(encoder) { // Capacity meter
            int64 fileSize = File(encoder->path, currentWorkingDirectory()).size();
            int64 available = ::available(encoder->path, currentWorkingDirectory());
            float x = int64(size.x-2*offset.x) * fileSize / (fileSize+available);
            graphics->fills.append(vec2(offset.x, 0), vec2(offset.x + x, offset.y), green);
            graphics->fills.append(vec2(offset.x + x, 0), vec2(size.x-offset.x - (offset.x + x), offset.y), black);
            int fileLengthSeconds = encodeVideo ? (encoder->videoTime+encoder->videoFrameRate-1)/encoder->videoFrameRate
                                                : (encoder->audioTime+encoder->audioFrameRate-1)/encoder->audioFrameRate;
            int64 fileSizeMB = (fileSize+1023) / 1024 / 1024;
            sizeText = Text(str(fileLengthSeconds)+"s "+str(fileSizeMB)+"MB", offset.y, white); // FIXME: skip if no change
            graphics->graphics.insertMulti(vec2(offset.x, 0), sizeText.graphics(int2(x, offset.y)));
            int availableLengthMinutes = int64(fileLengthSeconds) * available / fileSize / 60;
            int64 availableMB = available / 1024 / 1024;
            availableText = Text(str(availableLengthMinutes)+"min "+str(availableMB)+"MB", offset.y, white); // FIXME: skip if no change
            graphics->graphics.insertMulti(vec2(offset.x + x, 0), availableText.graphics(int2(size.x-offset.x-(offset.x+x), offset.y)));
        }
        for(int channel : range(audio.channels)) { // VU meters (actually mono)
            float y = size.y * (1 - smoothedMean / 0x1p28f); //clip(1.f, float(maximumAmplitude), 0x1p28f));
            graphics->fills.append(vec2(channel?offset.x+image.size.x:0, 0), vec2(offset.x, y), black);
            graphics->fills.append(vec2(channel?offset.x+image.size.x:0, y), vec2(offset.x, size.y-y), maximumAmplitude < ((1<<31)-1) ? green : red);
        }
        if(image) graphics->blits.append(vec2(offset), vec2(size-offset), share(image));
        return graphics;
    }
} app;
