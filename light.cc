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
    uint2 imageSize;
    array<Map> maps;
    ImageT<Image> images;

    struct View : ViewControl, ImageView {
        Light* _this;
        View(Light* _this) : _this(_this) {}

        virtual vec2 sizeHint(vec2) override { return 1024; }
        virtual shared<Graphics> graphics(vec2 size) override {
            vec4 viewRotation = qmul(angleVector(-viewYawPitch.y, vec3(1,0,0)), angleVector(viewYawPitch.x, vec3(0,1,0)));
            this->image = _this->render(uint2(size), viewRotation);
            return ImageView::graphics(size);
        }
    } view {this};
    unique<Window> window = nullptr;

    Light() {
        assert_(inputs);
        load(inputs[0]);
        window = ::window(&view);
        window->setTitle(name);
    }
    Image render(uint2 size, vec4 viewRotation) {
        Image target (size);
        target.clear(0xFF);

        // Rotated orthographic projection
        vec4 invViewRotation = conjugate(viewRotation);
        vec2 scale = size.x/2;
        vec2 offset = vec2(size)/2.f;

        for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
            vec3 Ov (x,y,0); // View space
            vec3 O = qapply(invViewRotation, ((Ov - vec3(offset, 0)) / vec3(scale, 1))); // World space
            vec3 d = qapply(invViewRotation, vec3(0, 0, 1));
            vec3 n (0,0,-1);
            vec2 st, uv;
            { vec3 M (0,0,0);
                vec3 P = O + dot(n, M-O) / dot(n, d) * d;
                st = (P.xy()+vec2(1))/2.f;
            }
            { vec3 M (0,0,1);
                vec3 P = O + dot(n, M-O) / dot(n, d) * d;
                uv = (P.xy()+vec2(1))/2.f;
            }

            st *= vec2(images.size-uint2(1));
            if(!(int2(0) <= int2(st) && int2(st) < int2(images.size-uint2(1)))) continue;
            uv *= vec2(imageSize-uint2(1));
            if(!(int2(0) <= int2(uv) && int2(uv) < int2(imageSize-uint2(1)))) continue;
            float s = st[0], t = st[1], u = uv[0], v = uv[1];

            bgr3f Sstuv [2][2][2][2];
            for(const int ds: {0, 1}) for(const int dt: {0, 1}) for(const int du: {0, 1}) for(const int dv: {0, 1}) {
                Sstuv[ds][dt][du][dv] = bgr3f(images(s+ds, t+dt)(u+du, v+dv).bgr());
            }

            bgr3f Sstu [2][2][2];
            float fv = fract(v);
            for(const int ds: {0, 1}) for(const int dt: {0, 1}) for(const int du: {0, 1})
                Sstu[ds][dt][du] = (1-fv) * Sstuv[ds][dt][du][0] + fv * Sstuv[ds][dt][du][1];

            bgr3f Sst [2][2];
            float fu = fract(u);
            for(const int ds: {0, 1}) for(const int dt: {0, 1})
                Sst[ds][dt] = (1-fu) * Sstu[ds][dt][0] + fu * Sstu[ds][dt][1];

            bgr3f Ss [2];
            float ft = fract(t);
            for(const int ds: {0, 1})
                Ss[ds] = (1-ft) * Sst[ds][0] + ft * Sst[ds][1];

            bgr3f S;
            float fs = fract(s);
            S = (1-fs) * Ss[0] + fs * Ss[1];

            target(x, y) = byte4(byte3(S), 0xFF);
        }
        return target;
    }
    void load(string name) {
        array<Image>(::move(images)).clear(); // Proper destruction in case heap allocated
        maps.clear();
        imageSize = 0;
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
                    goto break_;
                }
            } /*else*/ {
                log(name);
                Image image = decodeImage(Map(name, input));
                assert_(image.stride == image.size.x);
                String mapName = name+'.'+strx(image.size);
                writeFile(mapName, cast<byte>(image), tmp);
                images(x, y) = Image(cast<byte4>(unsafeRef(maps.append(mapName, tmp))), image.size);
            } break_:;
            if(!imageSize) imageSize = images(x, y).size;
            assert_(images(x,y).size == imageSize);
        }
        if(window) {
            window->setSize();
            window->setTitle(name);
        }
    }
} app;
