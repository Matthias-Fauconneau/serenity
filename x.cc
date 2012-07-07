#include "process.h"
#include "debug.h"
#include "linux.h"
#include "stream.h"
#include "file.h"

//TODO: use DataStream Socket
template<class T> inline T read(int fd) {
    T t;
    int unused size = read(fd,(byte*)&t,sizeof(T));
    assert(size==sizeof(T),size,sizeof(T));
    return t;
}

#include "X11/Xproto.h"
struct ConnectionSetup {
    int16 status;
    int32 major,minor;
    int16 vendor;
    int32 release, ridBase, ridMask, motionBufferSize;
    int16 nbytesVendor, maxRequestSize;
    int8 numRoots, numFormats, imageByteOrder, bitmapBitOrder, bitmapScanlineUnit, bitmapScanlinePad, minKeyCode, maxKeyCode;
    int32 pad2;
};

struct Test : Application {
    Test(array<string>&&) {
        log("Hello World!");
        catchErrors();
        int fd = socket(PF_UNIX, SOCK_STREAM, 0);
        string path = "/tmp/.X11-unix/X0"_;
        sockaddr_un addr; copy(addr.path,path.data(),path.size());
        if(connect(fd,(sockaddr*)&addr,3+path.size())) error("Connection failed");
        write(fd, "\x6c\x00\x0b\x00\x00\x00\x12\x00\x10\x00\x00\x00"_+slice(readFile("root/.Xauthority"_),18,18+2+16));
        auto c = read<ConnectionSetup>(fd);
        log(c.vendor,read(fd,c.vendor));
    }
};
Application(Test)
