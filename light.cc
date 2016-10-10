#include "file.h"
#include "parallel.h"
#include "scene.h"
#include "png.h"
#include "interface.h"
#include "window.h"

Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};

mat4 shearedPerspective(const float s, const float t) { // Sheared perspective (rectification)
    const float S = 2*s-1, T = 2*t-1; // [0,1] -> [-1, 1]
    const float left = (-1-S), right = (1-S);
    const float bottom = (-1-T), top = (1-T);
    mat4 M;
    M(0,0) = 2 / (right-left);
    M(1,1) = 2 / (top-bottom);
    M(0,2) = (right+left) / (right-left);
    M(1,2) = (top+bottom) / (top-bottom);
    const float near = 1-1./2, far = 1+1./2;
    M(2,2) = - (far+near) / (far-near);
    M(2,3) = - 2*far*near / (far-near);
    M(3,2) = - 1;
    M(3,3) = 0;
    M.translate(vec3(-S,-T,0));
    M.translate(vec3(0,0,-1)); // 0 -> -1 (Z-)
    return M;
}

struct Render {
    Render() {
        Scene scene;
        Folder folder {"synthetic", tmp, true};
        for(string file: folder.list(Files)) remove(file, folder);

        const size_t N = 129;
        uint2 size = 128;

        File file(str(N)+'x'+str(N)+'x'+strx(size), folder, Flags(ReadWrite|Create));
        size_t byteSize = 3*N*N*size.y*size.x*sizeof(half);
        assert_(byteSize <= 24ull*1024*1024*1024);
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);
        profile( field.clear() ); /*Explicitly clears to avoid performance skew from clear on page faults*/

        buffer<Scene::Renderer> renderers (threadCount());
        for(Scene::Renderer& renderer: renderers) new (&renderer) Scene::Renderer(scene);

        Time time (true);
        parallel_for(0, N*N, [&](uint threadID, size_t stIndex) {
            int sIndex = stIndex%N, tIndex = stIndex/N;

            // Sheared perspective (rectification)
            const float s = sIndex/float(N-1), t = tIndex/float(N-1);
            mat4 M = shearedPerspective(s, t);

            ImageH B (unsafeRef(field.slice(((0ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH G (unsafeRef(field.slice(((1ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH R (unsafeRef(field.slice(((2ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);

            scene.render(renderers[threadID], B, G, R, M);

            if(sIndex%32==0 && tIndex%32==0) writeFile(str(sIndex)+'_'+str(tIndex)+".png", encodePNG(convert(B, G, R)), folder, true);
        });
        log(time);
    }
}; // render;

struct ScrollValue : virtual Widget {
    int minimum = 0, maximum = 0;
    int value = 1;
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
            viewYawPitch = dragStart.viewYawPitch + float(2*PI) * (cursor - dragStart.cursor) / size;
            viewYawPitch.x = clamp<float>(-PI/3, viewYawPitch.x, PI/3);
            viewYawPitch.y = clamp<float>(-PI/3, viewYawPitch.y, PI/3);
        }
        else return false;
        return true;
    }
};

struct Light {
    buffer<String> inputs = currentWorkingDirectory().list(Folders);

    string name;
    vec2 min, max;
    uint2 imageCount;
    uint2 imageSize;
    Map map;
    ref<half> field;
    Scene scene;
    Scene::Renderer renderer {scene};

    bool orthographic = false;
    bool sample = true, raycast = true;

    struct View : ScrollValue, ViewControl, ImageView {
        Light& _this;
        View(Light& _this) : ScrollValue(0, _this.inputs.size-1), _this(_this) {}

        virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
            return ScrollValue::mouseEvent(cursor,size,event,button,widget) || ViewControl::mouseEvent(cursor,size,event,button,widget);
        }
        virtual vec2 sizeHint(vec2) override { return 1024; }
        virtual shared<Graphics> graphics(vec2 size) override {
            this->image = _this.render(uint2(size));
            return ImageView::graphics(size);
        }
    } view {*this};
    unique<Window> window = nullptr;

    Light() {
        assert_(arguments() || inputs);
        load(arguments() ? arguments()[0] : inputs[0]);
        window = ::window(&view);
        window->setTitle(name);
        window->actions[Key('s')] = [this]{ sample=!sample; window->render(); };
        window->actions[Key('r')] = [this]{ raycast=!raycast; window->render(); };
        window->actions[Key('o')] = [this]{ orthographic=!orthographic; window->render(); };
    }
    Image render(uint2 targetSize) {
        size_t fieldSize = (size_t)imageCount.y*imageCount.x*imageSize.y*imageSize.x;
        struct Image4DH : ref<half> {
            uint4 size;
            Image4DH(uint2 imageCount, uint2 imageSize, ref<half> data) : ref<half>(data), size(imageCount.y, imageCount.x, imageSize.y, imageSize.x) {}
            const half& operator ()(uint s, uint t, uint u, uint v) const {
                size_t index = (((uint64)t*size[1]+s)*size[2]+v)*size[3]+u;
                assert_(index < ref<half>::size, int(index), ref<half>::size, (int)s, (int)t, (int)u, (int)v, size);
                return operator[](index);
            }
        }   fieldB {imageCount, imageSize, field.slice(0*fieldSize, fieldSize)},
            fieldG {imageCount, imageSize, field.slice(1*fieldSize, fieldSize)},
            fieldR {imageCount, imageSize, field.slice(2*fieldSize, fieldSize)};

        Image target (targetSize);

        mat4 M;
        if(orthographic) {
            M.rotateX(view.viewYawPitch.y); // Pitch
            M.rotateY(view.viewYawPitch.x); // Yaw
            M.scale(vec3(1,1,-1)); // Z-
        } else {
            // Sheared perspective (rectification)
            const float s = (view.viewYawPitch.x+PI/2)/PI, t = (view.viewYawPitch.y+PI/2)/PI;
            M = shearedPerspective(s, t);
        }

        if(raycast) {
            ({ size_t start=0, sizeI=target.size.y*target.size.x; //parallel_chunk(target.size.y*target.size.x, [&](uint, size_t start, size_t sizeI) {
                for(size_t i: range(start, start+sizeI)) {
                    int y = i/target.size.x, x = i%target.size.x;
                    const vec3 O = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, 2.f*y/float(target.size.y-1)-1, -1);
                    const vec3 P = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, 2.f*y/float(target.size.y-1)-1, 1);
                    const vec3 d = normalize(P-O);

                    bgr3f S;
                    if(sample) {
                        const vec3 n (0,0,-1);
                        const float nd = dot(n, d);
                        const vec3 n_nd = n / nd;

                        const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,1)-O) * d.xy();
                        const vec2 ST = (Pst+vec2(1))/2.f;
                        const vec2 Puv = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
                        const vec2 UV = (Puv+vec2(1))/2.f;

                        const vec2 st = ST * vec2(imageCount-uint2(1));
                        const vec2 uv = UV * vec2(imageSize-uint2(1));

                        if(st[0] < 0 || st[1] < 0 || uv[0] < 0 || uv[1] < 0) { target(x,y)=byte4(byte3(0), 0xFF); continue; }
                        int sIndex = st[0], tIndex = st[1], uIndex = uv[0], vIndex = uv[1];
                        if(sIndex >= int(imageCount.x)-1 || tIndex >= int(imageCount.y)-1) { target(x,y)=byte4(byte3(0), 0xFF); continue; }
                        if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { target(x,y)=byte4(byte3(0), 0xFF); continue; }

                        float fs = fract(st[0]), ft = fract(st[1]), fu = fract(uv[0]), fv = fract(uv[1]);

                        v16hf blue, green, red;
                        for(const int ds: {0, 1}) for(const int dt: {0, 1}) for(const int du: {0, 1}) for(const int dv: {0, 1}) {
                            blue[((ds*2+dt)*2+du)*2+dv] = fieldB(sIndex+ds, tIndex+dt, uIndex+du, vIndex+dv);
                            green[((ds*2+dt)*2+du)*2+dv] = fieldG(sIndex+ds, tIndex+dt, uIndex+du, vIndex+dv);
                            red[((ds*2+dt)*2+du)*2+dv] = fieldR(sIndex+ds, tIndex+dt, uIndex+du, vIndex+dv);
                        }

                        bgr3f Sstuv [2][2][2][2];
                        v16sf B = toFloat(blue), G = toFloat(green), R = toFloat(red);
                        for(const int ds: {0, 1}) for(const int dt: {0, 1}) for(const int du: {0, 1}) for(const int dv: {0, 1}) {
                            Sstuv[ds][dt][du][dv] = bgr3f( B[((ds*2+dt)*2+du)*2+dv], G[((ds*2+dt)*2+du)*2+dv], R[((ds*2+dt)*2+du)*2+dv] );
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

                        S = (1-fs) * Ss[0] + fs * Ss[1];
                    } else {
                        S = scene.raycast(O, d);
                    }
                    target(x, y) = byte4(byte3(float(0xFF)*S), 0xFF);
                }
            });
                //convert(BGR, B, G, R); resize(target, BGR);
        } else {
            ImageH B (target.size), G (target.size), R (target.size);
            //B.clear(0); G.clear(0); R.clear(0); // DEBUG
            scene.render(renderer, B, G, R, M);
            convert(target, B, G, R);
        }
        return target;
    }
    void load(string name) {
        field = {};
        map = Map();
        imageCount = 0;
        imageSize = 0;
        this->name = name;
        Folder input (name);
        Folder tmp (name, ::tmp, true);

        for(string name: tmp.list(Files)) {
            TextData s (name);
            imageCount.x = s.integer(false);
            if(!s.match('x')) continue;
            imageCount.y = s.integer(false);
            if(!s.match('x')) continue;
            imageSize.x = s.integer(false);
            if(!s.match('x')) continue;
            imageSize.y = s.integer(false);
            assert_(!s);
            map = Map(name, tmp);
            field = cast<half>(map);
            break;
        }

        if(window) {
            window->setSize();
            window->setTitle(name);
        }
    }
} view;
