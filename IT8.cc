#include "data.h"
#include "color.h"
#include "interface.h"
#include "window.h"
#include "png.h"

struct WindowView : ImageView { Window window {this, int2(24*768/16, 768)}; WindowView(Image&& image) : ImageView(move(image)) {} };

Image4f parseIT8(ref<byte> it8) {
    TextData s(it8);
    Image4f target (24, 16);
    s.until("BEGIN_DATA\r\n");
    assert_(s);
    for(int i: range(12)) for(int j: range(1, 22 +1)) {
        s.skip(char('A'+i)+str(j));
        s.whileAny(' '); float x = s.decimal() / 100;
        s.whileAny(' '); float y = s.decimal() / 100;
        s.whileAny(' '); float z = s.decimal() / 100;
        log(x, y, z);
        s.until("\r\n");
        target(1+(j-1), 1+i) = {x,y,z,0};
    }
    for(int j: range(23 +1)) {
        s.skip("GS"+str(j));
        s.whileAny(' '); float x = s.decimal() / 100;
        s.whileAny(' '); float y = s.decimal() / 100;
        s.whileAny(' '); float z = s.decimal() / 100;
        s.until("\r\n");
        target(j, 1+12+1) = target(j, 1+12+1+1) = {x,y,z,0};
    }
    v4sf GS11 = target(11, 1+12+1);
    for(int j: range(24)) target(j, 0) = target(j, 1+12) = GS11;
    for(int i: range(13)) target(0, i) = target(23, i) = GS11;
    s.skip("END_DATA\r\n");
    assert_(!s);
    return target;
}

Image4f XYZtoBGR(Image4f&& source) {
    for(v4sf& value: source) {
        bgr3f bgr = XYZtoBGR(value[0], value[1], value[2]);
        value = {bgr[0], bgr[1], bgr[2], 0};
    }
    return move(source);
}

struct IT8 {
    string fileName = arguments()[0];
    string name = section(fileName,'.');
    Image target = resize(int2(24,16)*48, convert(XYZtoBGR(parseIT8(readFile(fileName)))));
};

struct Preview : IT8, WindowView, Application { Preview() : WindowView(move(target)) {} };
registerApplication(Preview);
struct Export : IT8, Application { Export() { writeFile(name+".png", encodePNG(target)); } };
registerApplication(Export, export);

