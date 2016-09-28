#include "png.h"
#include "interface.h"
#include "window.h"

struct ViewControl : virtual Widget {
    vec2 viewYawPitch = vec2(0, 0); // Current view angles

    struct {
     vec2 cursor;
     vec2 viewYawPitch;
    } dragStart;

    // Orbital ("turntable") view control
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button,
                                   Widget*&) override {
     if(event == Press) dragStart = {cursor, viewYawPitch};
     if(event==Motion && button==LeftButton) {
      viewYawPitch = dragStart.viewYawPitch + float(2*PI) * (cursor - dragStart.cursor) / size;
      viewYawPitch.x = clamp<float>(-PI/2, viewYawPitch.x, PI/2);
      viewYawPitch.y = clamp<float>(-PI/2, viewYawPitch.y, PI/2);
     }
     else return false;
     return true;
    }
};

struct Light {
    Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};
    buffer<String> inputs = currentWorkingDirectory().list(Folders);

    string name;
    array<Map> maps;
    ImageT<Image> images;

    struct LightView : ViewControl {
        Light* _this;
        LightView(Light* _this) : _this(_this) {}

        virtual vec2 sizeHint(vec2) override { return 1024; }
        virtual shared<Graphics> graphics(vec2 size) override {
         shared<Graphics> graphics;
         // Rotated orthographic projection
         vec4 viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)), angleVector(viewYawPitch.x, vec3(0,1,0)));
         vec2 scale = size.x/2;
         vec2 offset = size/2.f;
         for(float z: {0, 1}) for(size_t i: range(4)) { // st, uv
          vec2 P[] {vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,1)};
          vec2 P1 = offset + scale * qapply(viewRotation, vec3(P[i], z)).xy();
          vec2 P2 = offset + scale * qapply(viewRotation, vec3(P[(i+1)%4], z)).xy();
          assert_(isNumber(P1) && isNumber(P2));
          graphics->lines.append(P1, P2);
         }
         return ::move(graphics);
        }
    } view {this};
    unique<Window> window = nullptr;

    Light() {
        assert_(inputs);
        load(inputs[0]);
        window = ::window(&view);
        window->setTitle(name);
    }
#if 0
    void setImage(vec3 value) {
        if(inputs[size_t(value.z)] != this->name) load(inputs[size_t(value.z)]);
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
    }
#endif
    void load(string unused name) {
#if 0
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
#endif
    }
} app;
