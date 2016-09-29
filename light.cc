#include "file.h"
Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};

#if 1
#include "image.h"
#include "png.h"

struct Render {
    Render() {
        Folder folder {"synthetic", tmp, true};
        const int N = 17;
        Image target (1024);
        for(int sIndex: range(N)) for(int tIndex: range(N)) {
            float s = sIndex/float(N-1), t = tIndex/float(N-1);
            target.clear(0);
            for(int vIndex: range(target.size.y)) for(int uIndex: range(target.size.x)) {
                float u = uIndex/float(target.size.x-1), v = vIndex/float(target.size.y-1);
                vec3 O (s, t, 0); // World space ray origin
                vec3 D (u, v, 1); // World space ray destination
                vec3 d = normalize(D-O); // World space ray direction (sheared perspective pinhole)
                // Object (Plane)
                vec3 n (0,0,-1);
                vec3 M (vec2(1./2), 1);
                vec3 P = O + dot(n, M-O) / dot(n, d) * d;
                vec2 xy = P.xy(); //(P.xy()+vec2(1))/2.f;
                //log(O, D, d, P);
                //assert_(xy >= vec2(0-0x1p-20) && xy <= vec2(1+0x1p-20), s, t, u, v, O, xy);
                target(uIndex, vIndex) = byte4(byte2(float(0xFF)*min(xy, vec2(1))), 0, 0xFF);
            }
            writeFile(str(tIndex)+'_'+str(sIndex)+'.'+strx(target.size), cast<byte>(target), folder, true);
            //writeFile(str(tIndex)+'_'+str(sIndex)+".png", encodePNG(target), folder, true);
        }
    }
} app;

#endif
#if 1

#include "png.h"
#include "interface.h"
#include "window.h"

struct ScrollValue : virtual Widget {
    int minimum = 0, maximum = 0;
    int value = 0;
    ScrollValue(int minimum, int maximum) : minimum(minimum), maximum(maximum) {}
    virtual bool mouseEvent(vec2, vec2, Event event, Button button, Widget*&) override {
        int value = this->value;
        if(event == Press && (button == WheelUp || button == WheelDown))
            value = int(this->value+maximum+(button==WheelUp?1:-1))%(maximum+1);
        if(value != this->value) { this->value = value; return true; }
        return false;
    }
};

struct ViewControl : virtual Widget {
    vec2 viewYawPitch = vec2(0, 0); // Current view angles

    struct {
        vec2 cursor;
        vec2 viewYawPitch;
    } dragStart;

    // Orbital ("turntable") view control
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        if(event == Press) dragStart = {cursor, viewYawPitch};
        if(event==Motion && button==LeftButton) {
            viewYawPitch = dragStart.viewYawPitch + float(2*PI) * (cursor - dragStart.cursor) / size;// / 100.f;
            viewYawPitch.x = clamp<float>(-PI/2, viewYawPitch.x, PI/2);
            viewYawPitch.y = clamp<float>(-PI/2, viewYawPitch.y, PI/2);
        }
        else return false;
        return true;
    }
};

struct Light {
    buffer<String> inputs = currentWorkingDirectory().list(Folders);

    string name;
    vec2 min, max;
    uint2 imageSize;
    array<Map> maps;
    ImageT<Image> images;

    struct View : ScrollValue, ViewControl, ImageView {
        Light& _this;
        View(Light& _this) : ScrollValue(0, _this.inputs.size-1), _this(_this) {}

        virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
            return ScrollValue::mouseEvent(cursor,size,event,button,widget) || ViewControl::mouseEvent(cursor,size,event,button,widget);
        }
        virtual vec2 sizeHint(vec2) override { return 1024; }
        virtual shared<Graphics> graphics(vec2 size) override {
            vec4 viewRotation = qmul(angleVector(-viewYawPitch.y, vec3(1,0,0)), angleVector(viewYawPitch.x, vec3(0,1,0)));
            this->image = _this.render(uint2(size), viewRotation);
            return ImageView::graphics(size);
        }
    } view {*this};
    unique<Window> window = nullptr;

    Light() {
        assert_(arguments() || inputs);
        load(arguments() ? arguments()[0] : inputs[0]);
        window = ::window(&view);
        window->setTitle(name);
    }
    Image render(uint2 size, vec4 viewRotation) {
        //if(inputs[view.value] != this->name) load(inputs[view.value]);
        Image target (size);
        target.clear(0);

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
            { vec3 M (0,0,1);
                vec3 P = O + dot(n, M-O) / dot(n, d) * d;
                st = (P.xy()+vec2(1))/2.f;
            }
            { vec3 M (0,0,0);
                vec3 P = O + dot(n, M-O) / dot(n, d) * d;
                uv = (P.xy()+vec2(1))/2.f;
            }

            //st[1] = 1-st[1];
            if(1) {
                st *= vec2(images.size-uint2(1));
            } else {
                if(x==target.size.x/2 && y==target.size.y/2) log(1, st);
                st *= imageSize.y-1;
                if(x==target.size.x/2 && y==target.size.y/2) log(2, st);
                // Converts from pixel to image indices
                st = st - min + vec2(imageSize)/2.f; // Corner to center
                if(x==target.size.x/2 && y==target.size.y/2) log(3, st);
                st *= vec2(images.size-uint2(1));
                if(x==target.size.x/2 && y==target.size.y/2) log(4, st);
                st[0] = st[0] / (max-min)[0];
                st[1] = st[1] / (max-min)[1];
                if(x==target.size.x/2 && y==target.size.y/2) log(5, st);
                st -= vec2(images.size-uint2(1))/2.f; // Center to corner
                if(x==target.size.x/2 && y==target.size.y/2) log(6, st);
            }

            //uv[1] = 1-uv[1];
            uv *= imageSize.x-1;
            int is = st[0], it = st[1], iu = uv[0], iv = uv[1];
            if(is < 0 || is >= int(images.size.x)-1 || it < 0 || it >= int(images.size.y)-1) continue;
            if(iu < 0 || iu >= int(imageSize.x)-1 || iv < 0 || iv >= int(imageSize.y)-1) continue;
            float fs = fract(st[0]), ft = fract(st[1]), fu = fract(uv[0]), fv = fract(uv[1]);

            bgr3f Sstuv [2][2][2][2];
            for(const int ds: {0, 1}) for(const int dt: {0, 1}) for(const int du: {0, 1}) for(const int dv: {0, 1}) {
                Sstuv[ds][dt][du][dv] = bgr3f(images(is+ds, it+dt)(iu+du, iv+dv).bgr());
            }

            bgr3f Sstu [2][2][2];
            for(const int ds: {0, 1}) for(const int dt: {0, 1}) for(const int du: {0, 1})
                Sstu[ds][dt][du] = (1-fv) * Sstuv[ds][dt][du][0] + fv * Sstuv[ds][dt][du][1];

            bgr3f Sst [2][2];
            for(const int ds: {0, 1}) for(const int dt: {0, 1})
                Sst[ds][dt] = (1-fu) * Sstu[ds][dt][0] + fu * Sstu[ds][dt][1];

            bgr3f Ss [2];
            for(const int ds: {0, 1})
                Ss[ds] = (1-ft) * Sst[ds][0] + ft * Sst[ds][1];

            bgr3f S;
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
        Folder tmp (name, ::tmp, true);

        range xRange {0}, yRange {0};
        min = vec2(inff), max = vec2(-inff);
        for(string name: input.list(Files)) {
            TextData s (name);
            if(find(name, ".png")) s.until('_');
            int y = s.integer();
            s.match('_');
            int x = s.integer();

            xRange.start = ::min(xRange.start, x);
            xRange.stop = ::max(xRange.stop, x+1);

            yRange.start = ::min(yRange.start, y);
            yRange.stop = ::max(yRange.stop, y+1);

            if(0) {
                s.match('_');
                float py = s.decimal();
                s.match('_');
                float px = s.decimal();
                min = ::min(min, vec2(px, py));
                max = ::max(max, vec2(px, py));
            }
        }
        if(0) {
            min.y = -min.y;
            max.y = -max.y;
            swap(min.y, max.y);
        }
        if(0) log(min, max, max-min);

        images = ImageT<Image>(uint(xRange.size()), uint(yRange.size()));
        images.clear(Image());

        for(string name: input.list(Files)) {
            TextData s (name);
            if(find(name, ".png")) s.until('_');
            uint y = uint(s.integer(false));
            s.match('_');
            uint x = uint(s.integer(false));

            for(string mapName: tmp.list(Files)) {
                if(find(mapName, name)) {
                    TextData s (mapName);
                    if(find(mapName, ".png.")) s.until(".png."); else s.until('.');
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

#endif
