#include <linux/videodev2.h>
#include "video-input.h"

typedef IOR<'V', 0, v4l2_capability> QueryCapabilities;
typedef IOWR<'V', 2, v4l2_fmtdesc> EnumerateFormats;
typedef IOWR<'V', 5, v4l2_format> SetFormat;
typedef IOWR<'V', 74, v4l2_frmsizeenum> EnumerateFrameSizes;
typedef IOWR<'V', 75, v4l2_frmivalenum> EnumerateFrameIntervals;
typedef IOWR<'V', 8, v4l2_requestbuffers> RequestBuffers;
typedef IOWR<'V', 9, v4l2_buffer> QueryBuffer;
typedef IOWR<'V', 15, v4l2_buffer> QueueBuffer;
typedef IOWR<'V', 17, v4l2_buffer> DequeueBuffer;
typedef IOW<'V', 18, int> StartStream;
typedef IOWR<'V', 21, v4l2_streamparm> GetParameters;
typedef IOWR<'V', 22, v4l2_streamparm> SetParameters;
typedef IOW<'V', 68, uint32> SetPriority;

void VideoInput::setup() {
    //for(v4l2_fmtdesc fmt {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .index=0}; iowr<EnumerateFormats>(fmt, -1) != int(-1); fmt.index++)
    //   log(fmt.type, fmt.index, (const char*)fmt.description, fmt.flags);
    //for(v4l2_frmsizeenum frm {.index=0, .pixel_format=V4L2_PIX_FMT_YUYV/*V4L2_PIX_FMT_MJPEG*/}; iowr<EnumerateFrameSizes>(frm, -1) != int(-1); frm.index++) {
    //    log(frm.discrete.width, frm.discrete.height);
    //    width=frm.discrete.width, height=frm.discrete.height;
    //}
    //log(width, height);
    width = 1280, height = 720;
    /*for(v4l2_frmivalenum frm = {.index=0, .pixel_format=V4L2_PIX_FMT_MJPEG, .width=width, .height=height}; iowr<EnumerateFrameIntervals>(frm, -1) != int(-1);
        frm.index++) {
        assert_(frm.type==V4L2_FRMIVAL_TYPE_DISCRETE && frm.discrete.numerator == 1);
        frameRate = frm.discrete.denominator;
        log(frameRate);
    }*/
    frameRate = 30;
    v4l2_format fmt = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .fmt.pix={.width=width, .height=height, .pixelformat=V4L2_PIX_FMT_MJPEG, .field=0, .bytesperline=0,
                                                                      .sizeimage=0/*uint(size.y*size.x)*/, .colorspace=0, .priv=0}};
    iowr<SetFormat>(fmt);
    width=fmt.fmt.pix.width, height=fmt.fmt.pix.height;
    assert_(width==1280 && height==720);
    v4l2_streamparm parm {.type=V4L2_BUF_TYPE_VIDEO_CAPTURE};
    iowr<GetParameters>(parm);
    assert_(parm.parm.capture.timeperframe.numerator == 1);
    frameRate = parm.parm.capture.timeperframe.denominator;
    assert_(frameRate == 30, frameRate);
    iow<SetPriority>(V4L2_PRIORITY_RECORD);
    const int bufferedFrameCount = 4;
    v4l2_requestbuffers req = {.count = bufferedFrameCount, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP};
    iowr<RequestBuffers>(req);
    assert_(req.count == bufferedFrameCount);
    for(uint bufferIndex: range(req.count)) {
        v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .index = bufferIndex};
        iowr<QueryBuffer>(buf);
        buffers.append(Map(Device::fd, buf.m.offset, buf.length, Map::Prot(Map::Read|Map::Write)));
        iowr<QueueBuffer>(buf);
    }
}

void VideoInput::start() {
    iow<StartStream>(V4L2_BUF_TYPE_VIDEO_CAPTURE);
}

void VideoInput::event() {
    v4l2_buffer buf = {.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP, .reserved={}};
    iowr<DequeueBuffer>(buf);
    if(buf.sequence != videoTime) {
        log("Dropped", buf.sequence-videoTime, "video frame"+((buf.sequence-videoTime)>1?"s"_:""_));
        videoTime = buf.sequence;
    }
    uint64 timeStamp = uint64(buf.timestamp.tv_sec)*1000000ull + uint64(buf.timestamp.tv_usec);
    //log(timeStamp);
    write(buffers[buf.index].slice(0, buf.bytesused), timeStamp);
    if(lastTimeStamp && timeStamp-lastTimeStamp > 36017/*33600*/) log("Delay", timeStamp-lastTimeStamp);
    lastTimeStamp = timeStamp;
    iowr<QueueBuffer>(buf);
    videoTime++;
}

#if 0
struct VideoInputTest {
    VideoInput video{{this, &VideoInputTest::bufferVideo}};
    VideoInputTest() {
        video.setup();
        video.start();
    }
    void bufferVideo(ref<byte>) {}
} test;
#endif
