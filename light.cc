#if 1
#include "interface.h"
#include "render.h"
#include "window.h"

inline String str(range r) { return str(r.start, r.stop); } // -> string.h

struct Light {
    Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};
    buffer<String> inputs = currentWorkingDirectory().list(Folders);

    string name;
    ImageView view;
    unique<Window> window = nullptr;

    Light() {
        assert_(inputs);
        load(inputs[0]);
        window = ::window(&view);
        window->setTitle(name);
    }
    void load(string name) {
        this->name = name;
        Folder input (name);
        Folder tmp (name, this->tmp, true);

        range xRange {0}, yRange {0};
        for(string name: input.list(Files)) {
            TextData s (name);
            s.until('_');
            int y = s.integer();
            s.match('_');
            int x = s.integer();

            xRange.start = ::min(xRange.start, x);
            xRange.stop = ::max(xRange.stop, x+1);

            yRange.start = ::min(yRange.start, y);
            yRange.stop = ::max(yRange.stop, y+1);
        }

        int2 size (0, 0);
        for(string name: input.list(Files)) {
            TextData s (name);
            s.until('_');
            unused uint y = uint(s.integer(false));
            s.match('_');
            unused uint x = uint(s.integer(false));

            int2 imageSize = ::imageSize(Map(name, input));
            if(!size) size = imageSize;
            assert_(imageSize == size);
        }

        Image image(1024);
        log(xRange, size);
        const int M = 8, N = 64;
        for(int s: range(xRange.size()/M+1)) /*for(int t: yRange)*/ for(int u: range(size.x/N)) /*for(int v: range(size.y))*/ {
            float x0 = (s*M-xRange.start)*(image.size.x-1)/(xRange.size()-1);
            float x1 = (u*N-0)*(image.size.x-1)/(size.x-1);
            line(image, vec2(x0, 0), vec2(x1, image.size.y), white);
        }
        view.image = ::move(image);
    }
} app;

#else
#include "png.h"
#include "interface.h"
#include "window.h"

struct SliderSurface : virtual Widget {
    //function<void(int3)> valueChanged;
    virtual void valueChanged(vec3) {}
    int3 minimum, maximum;
    vec3 value;
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override;
};
bool SliderSurface::mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) {
    vec3 value = this->value;
    if(event == Press && (button == WheelUp || button == WheelDown)) {
     value.z = (int(this->value.z)+maximum.z+(button==WheelUp?1:-1))%(maximum.z+1);
    }
    if((event == Motion || event==Press) && button==LeftButton) {
        value.xy() = clamp(vec2(minimum.xy()), vec2(minimum.xy())+cursor*vec2(maximum.xy()-minimum.xy())/size, vec2(maximum.xy()));
        //assert_(minimum <= value && value <= maximum, minimum, value, maximum);
    }
    if(value != this->value) {
        this->value = value;
        /*if(valueChanged)*/ {
            valueChanged(value);
            return true;
        }
    }
    return false;
}

struct SliderView : ImageView, SliderSurface { ~SliderView(); };
SliderView::~SliderView() {}

struct Light {
    Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};
    buffer<String> inputs = currentWorkingDirectory().list(Folders);

    string name;
    array<Map> maps;
    ImageT<Image> images;
    //SliderView view;
    struct SliderView : ::SliderView {
        Light* _this;
        SliderView(Light* _this) : _this(_this) {}
        virtual void valueChanged(vec3 value) override { _this->setImage(value); }
    } view {this};
    unique<Window> window = nullptr;

    Light() {
        //view.valueChanged = {this, &Light::setImage};
        assert_(inputs);
        load(inputs[0]);
        window = ::window(&view);
        window->setTitle(name);
    }
    void setImage(vec3 value) {
        if(inputs[size_t(value.z)] != this->name) load(inputs[size_t(value.z)]);
#if 1
        vec2 st (images.size.x-1-value.x, value.y);
        st = min(st, vec2(images.size-uint2(1))-vec2(1./2));
        assert(vec2(0) <= st && st < vec2(images.size-uint2(1)));
        const Image& st00 = images(st.x, st.y);
        const Image& st10 = images(st.x+1, st.y);
        const Image& st01 = images(st.x, st.y+1);
        const Image& st11 = images(st.x+1, st.y+1);
        vec2 f = fract(st);
        Image image (st00.size);
        for(size_t y: range(image.size.y)) for(size_t x: range(image.size.x)) {
            image(x, y) = byte4(byte3(
                    (1-f.y) * ( (1-f.x) * bgr3f(st00(x, y).bgr()) + f.x * bgr3f(st10(x, y).bgr()) )
                    +  f.y  * ( (1-f.x) * bgr3f(st01(x, y).bgr()) + f.x * bgr3f(st11(x, y).bgr()) )), 0);
        }
        view.image = ::move(image);
#elif 0
        view.image = unsafeShare(images(images.size.x-1-uint(value.x), uint(value.y)));
#else
        Image image (images.size);
        for(size_t y: range(images.size.y)) for(size_t x: range(images.size.x)) {
            image(x, y) = images(x, y)(value.x, value.y);
        }
        view.image = resize(image.size*128u, image);
#endif
    }
    void load(string name) {
        view.image = Image();
        array<Image>(::move(images)).clear(); // Proper destruction in case heap allocated
        maps.clear();
        this->name = name;
        Folder input (name);
        Folder tmp (name, this->tmp, true);

        range xRange {0}, yRange {0};
        for(string name: input.list(Files)) {
            TextData s (name);
            s.until('_');
            int y = s.integer();
            s.match('_');
            int x = s.integer();

            xRange.start = ::min(xRange.start, x);
            xRange.stop = ::max(xRange.stop, x+1);

            yRange.start = ::min(yRange.start, y);
            yRange.stop = ::max(yRange.stop, y+1);
        }

        images = ImageT<Image>(uint(xRange.size()), uint(yRange.size()));
        images.clear(Image());

        for(string name: input.list(Files)) {
            TextData s (name);
            s.until('_');
            uint y = uint(s.integer(false));
            s.match('_');
            uint x = uint(s.integer(false));

            for(string mapName: tmp.list(Files)) {
                if(find(mapName, name)) {
                    TextData s (mapName);
                    s.until(".png.");
                    uint w = uint(s.integer(false));
                    s.match('x');
                    uint h = uint(s.integer(false));
                    images(x, y) = Image(cast<byte4>(unsafeRef(maps.append(mapName, tmp))), uint2(w, h));
                    goto continue2_;
                }
            }
            {
                log(name);
                Image image = decodeImage(Map(name, input));
                assert_(image.stride == image.size.x);
                String mapName = name+'.'+strx(image.size);
                writeFile(mapName, cast<byte>(image), tmp);
                images(x, y) = Image(cast<byte4>(unsafeRef(maps.append(mapName, tmp))), image.size);
            }
            continue2_:;
        }
        if(1) {
            view.minimum = int3(xRange.start, yRange.start, 0);
            view.maximum = int3(xRange.stop-1, yRange.stop-1, inputs.size-1);
        } else {
            view.minimum = int3(int2(0, 0), 0);
            view.maximum = int3(int2(images[0].size-uint2(1)), inputs.size-1);
        }
        view.value = vec3((xRange.start+xRange.stop)/2, (yRange.start+yRange.stop)/2, inputs.indexOf(name));
        setImage(view.value);
        if(window) {
            window->setSize();
            window->setTitle(name);
        }
    }
} app;
#endif
