#include "process.h"
#include "debug.h"
#include "linux.h"
#include "stream.h"
#include "file.h"

//#include "font.h"
//#include "display.h"
//#include "text.h"
/*#include "zip.h"
#include "xml.h"
//#include "html.h"
//#include "interface.h"
//#include "window.h"

struct EPub : Application {
    //Scroll<Text> content;
    //Window window{&content.parent()};

    EPub(array<string>&& arguments) {
        //window.localShortcut("Escape"_).connect(this, &Application::quit);
        //window.show();
        Map zip = mapFile(arguments.first());
        map<string, ZipFile> files = readZip(array<byte>(zip));
        Element content = parseXML(files.at("content.opf"_));
        const Element& package = content("package"_);
        map<string, string> itemMap;
        for(const Element& item: package("manifest"_).children) itemMap.insert(item.at("id"_), item.at("href"_));
        array<string> items; for(const Element& itemref: package("spine"_).children) items << copy(itemMap.at(itemref.at("idref"_)));
        for(const string& item: items) {
            log(parseHTML(files.at(item)));
            break;
        }
    }
};
Application(EPub)*/

/*struct Test : Application {
    Text text i({"Hello World!"_});
    Test(array<string>&&) {
        catchErrors();
        openDisplay();
        text.position = int2(0,0); text.Widget::size = text.sizeHint();
        text.render(int2(0,0));
    }
};
Application(Test)*/

//TODO: use Socket Stream
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
    CARD32	   pad2 B32;
};

struct Test : Application {
    Test(array<string>&&) {
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
