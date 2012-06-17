#include "process.h"
#include "linux.h"
#include "debug.h"
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
        for(const auto& item: package("manifest"_).children) itemMap.insert(item.at("id"_), item.at("href"_));
        array<string> items; for(const auto& itemref: package("spine"_).children) items << copy(itemMap.at(itemref.at("idref"_)));
        for(const string& item: items) {
            log(parseHTML(files.at(item)));
            break;
        }
    }
};
Application(EPub)*/

struct VScreen { uint xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale; uint reserved[32]; };
struct FScreen { char id[16]; ulong smem_start; uint p1[4]; uint16 p2[3]; uint stride; ulong mmio; uint p3[2]; uint16 p4[3]; };
enum { FBIOGET_VSCREENINFO=0x4600, FBIOPUT_VSCREENINFO, FBIOGET_FSCREENINFO };

struct Test : Application {
    Test(array<string>&&) {
        int fd = open("/dev/fb0", O_RDWR, 0);
        FScreen fScreen; ioctl(fd, FBIOGET_FSCREENINFO, &fScreen);
        VScreen vScreen; ioctl(fd, FBIOGET_VSCREENINFO, &vScreen);
        int size = vScreen.xres * vScreen.yres * 2;
        uint16* fb = (uint16*)mmap(0, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        uint stride = fScreen.stride/2; //uint16*
        for(int y = 16; y < 32; y++) for(int x = 16; x < 32; x++) {
            uint16* p = fb + y * stride + x;
            int b = 10;
            int g = (x-100)/6;     // A little green
            int r = 31-(y-100)/16;    // A lot of red
            *p = r<<11 | g << 5 | b;
        }
    }
};
Application(Test)

