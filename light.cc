#include "file.h"
#include "image.h"
#include "png.h"
#include "interface.h"
#include "window.h"
#include "raster.h"

Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};

#if 0
static bool intersect(vec3 A, vec3 B, vec3 C, vec3 O, vec3 d, float& t, float& u, float& v) { //from "Fast, Minimum Storage Ray/Triangle Intersection"
    //if(dot(A-O, d)<=0 && dot(B-O, d)<=0 && dot(C-O, d)<=0) return false;
    vec3 edge1 = B - A;
    vec3 edge2 = C - A;
    vec3 pvec = cross(d, edge2);
    float det = dot(edge1, pvec);
    if(det < 0/*0x1p-16f*/) return false;
    vec3 tvec = O - A;
    u = dot(tvec, pvec);
    if(u < 0 || u > det) return false;
    vec3 qvec = cross(tvec, edge1);
    v = dot(d, qvec);
    if(v < 0 || u + v > det) return false;
    t = dot(edge2, qvec);
    t /= det;
    u /= det;
    v /= det;
    return true;
}

struct Scene {
    struct Face { vec3 position[3]; };
    Face faces[6*2]; // Cube
    Scene() {
            vec3 position[8];
            const float size = 1./2;
            for(int i: range(8)) position[i] = size * (2.f * vec3(i/4, (i/2)%2, i%2) - vec3(1)); // -1, 1
            const int indices[6*4] = { 0,2,3,1, 0,1,5,4, 0,4,6,2, 1,3,7,5, 2,6,7,3, 4,5,7,6};
            for(int i: range(6)) {
                faces[i*2+0] = {{position[indices[i*4+0]], position[indices[i*4+1]], position[indices[i*4+2]]}};
                faces[i*2+1] = {{position[indices[i*4+0]], position[indices[i*4+2]], position[indices[i*4+3]]}};
            }
    }
    bgr3f raycast(vec3 O, vec3 d) const {
        float minZ = inff; bgr3f color (0, 0, 0);
        for(Face face: faces) {
            float z, u, v;
            if(!intersect(face.position[0], face.position[1], face.position[2], O, d, z, u, v) || z>minZ) continue;
            color.b = u;
            color.g = v;
        }
        return color;
    }
};
#else
struct Scene {
    struct Face { vec3 position[3]; bgr3f color; };
    RenderTarget target; // Render target (RenderPass renders on these tiles)
    /// Shader for flat surfaces
    struct Shader {
        // Shader specification (used by rasterizer)
        struct FaceAttributes { bgr3f color; };
        static constexpr int V = 0;
        static constexpr bool blend = false; // Disables unnecessary blending

        bgra4f operator()(FaceAttributes face, float unused varying[V]) const {
            return bgra4f(face.color,1.f);
        }
    } shader;
    RenderPass<Shader> pass {shader};
    Face faces[6*2]; // Cube
    Scene() {
            vec3 position[8];
            const float size = 1./2;
            for(int i: range(8)) position[i] = size * (2.f * vec3(i/4, (i/2)%2, i%2) - vec3(1)); // -1, 1
            const int indices[6*4] = { 0,2,3,1, 0,1,5,4, 0,4,6,2, 1,3,7,5, 2,6,7,3, 4,5,7,6};
            const bgr3f colors[6] = {red, green, blue, cyan, magenta, yellow};
            for(int i: range(6)) {
                faces[i*2+0] = {{position[indices[i*4+0]], position[indices[i*4+1]], position[indices[i*4+2]]}, colors[i]};
                faces[i*2+1] = {{position[indices[i*4+0]], position[indices[i*4+2]], position[indices[i*4+3]]}, colors[i]};
            }
    }
    void render(Image& final, mat4 view) {
        target.setup(int2(final.size));
        pass.setup(target, ref<Face>(faces).size); // Resize if necessary and clears
        for(const Face& face: faces) {
            vec3 A = view*face.position[0], B = view*face.position[1], C = view*face.position[2];
            //if(cross(B-A,C-A).z <= 0) continue; // Backward face culling
            vec3 attributes[0];
            pass.submit(A,B,C, attributes, {face.color});
        }
        pass.render(target);
        target.resolve(final);
    }
};
#endif

#if 0
struct Render {
    Render() {
        const Scene scene;
        Folder folder {"synthetic", tmp, true};
        for(string file: folder.list(Files)) remove(file, folder);
        const int N = 33;
        Image target (256);
        Time time (true), lastReport(true);
        for(int sIndex: range(N)) {
            if(lastReport.seconds() > 1) { log(sIndex, "/", N, time); lastReport.reset(); }
            for(int tIndex: range(N)) {
                float s = 2*sIndex/float(N-1)-1, t = 2*tIndex/float(N-1)-1; // [-1, 1]
                target.clear(0);
#if 0
                for(int vIndex: range(target.size.y)) for(int uIndex: range(target.size.x)) {
                    float u = 2*uIndex/float(target.size.x-1)-1, v = 2*vIndex/float(target.size.y-1)-1; // [-1, 1]
                    vec3 O (s, t, -1); // World space ray origin
                    vec3 D (u, v, 0); // World space ray destination
                    vec3 d = normalize(D-O); // World space ray direction (sheared perspective pinhole)
                    target(uIndex, vIndex) = byte4(byte3(clamp(bgr3i(0), bgr3i(float(0xFF)*scene.raycast(O, d)), bgr3i(0xFF))), 0xFF); // FIXME: slow
                }
#else
                mat4 NDC;
                NDC.scale(vec3(1.f/(vec2(target.size)/2.f), 1)); // 0, 2
                NDC.translate(vec3(-1./2,0.f,0.f)); //-1, 1
                mat4 P;
                float near = 1-1./2, d = 1, far = 1+1./2;
                float left = s*near/d, right = (1-s)*near/d;
                float top = t*near/d, bottom = (1-t)*near/d;
                P(0,0) = 2*near;
                P(1,1) = 2*near;
                P(0,2) = (right-left)/(right+left);
                P(1,2) = (top-bottom)/(top+bottom);
                P(2,2) = (near+far) / (near-far);
                P(2,3) = 2*near*far / (near-far);
                P(3,2) = 1;
                P(3,3) = 0;
                scene.render(target, P * NDC);
#endif
                writeFile(str(tIndex)+'_'+str(sIndex)+'.'+strx(target.size), cast<byte>(target), folder, true);
                //writeFile(str(tIndex)+'_'+str(sIndex)+".png", encodePNG(target), folder, true);
            }
        }
        log(time);
    }
} render;

#endif

#if 1

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
    Scene scene;

    struct View : ScrollValue, ViewControl, ImageView {
        Light& _this;
        View(Light& _this) : ScrollValue(0, 2/*_this.inputs.size-1*/), _this(_this) {}

        virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
            return ScrollValue::mouseEvent(cursor,size,event,button,widget) || ViewControl::mouseEvent(cursor,size,event,button,widget);
        }
        virtual vec2 sizeHint(vec2) override { return 1024; }
        virtual shared<Graphics> graphics(vec2 size) override {
            vec4 viewRotation = qmul(angleVector(viewYawPitch.y, vec3(1,0,0)), angleVector(-viewYawPitch.x, vec3(0,1,0)));
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
        if(view.value) {
#if 0
            for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
                vec3 O = qapply(invViewRotation, vec3(2.f*x/float(target.size.x-1)-1, 2.f*y/float(target.size.y-1)-1, -1)); // [-1, 1]
                vec3 d = qapply(invViewRotation, vec3(0, 0, 1));
                target(x, y) = byte4(byte3(clamp(bgr3i(0), bgr3i(float(0xFF)*scene.raycast(O, d)), bgr3i(0xFF))), 0xFF);
            }
#else
            float s = (view.viewYawPitch.x+PI/2)/PI, t = 1-(view.viewYawPitch.y+PI/2)/PI;
            assert_(s >= 0 && s <= 1 && t >= 0 && t <= 1);
            mat4 NDC;
            NDC.scale(vec3(vec2(target.size*4u)/2.f, 1)); // 0, 2 -> subsample size
            NDC.translate(vec3(vec2(1),0.f)); // -1, 1 -> 0, 2
            mat4 P;
            float near = 1-1./2, far = 1+1./2;
            // Sheared perspective (rectification)
            float S = 2*s-1, T = 2*t-1; // [0,1] -> [-1, 1]
            float left = (-1-S), right = (1-S);
            float bottom = (-1-T), top = (1-T);
            P(0,0) = 2 / (right-left);
            P(1,1) = 2 / (top-bottom);
            P(0,2) = - (right+left) / (right-left);
            P(1,2) = - (top+bottom) / (top-bottom);
            P(2,2) = (far+near) / (far-near);
            P(2,3) = 2*far*near / (far-near);
            P(3,2) = 1;
            P(3,3) = 0;
            mat4 M;
            M.translate(vec3(-S,-T,0));
            M.translate(vec3(0,0,1)); // 0 -> 1 (Z)
            scene.render(target, NDC * P * M);
#endif
        } else {
            for(int y: range(target.size.y)) for(int x: range(target.size.x)) {
                vec3 O = qapply(invViewRotation, vec3(2.f*x/float(target.size.x-1)-1, 2.f*y/float(target.size.y-1)-1, -1)); // [-1, 1]
                vec3 d = qapply(invViewRotation, vec3(0, 0, 1));

                vec3 n (0,0,-1);
                vec2 st, uv;
                { vec3 M (0,0,-1);
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
                    st *= 1.f/(max-min);
                    if(x==target.size.x/2 && y==target.size.y/2) log(5, st);
                    st -= vec2(images.size-uint2(1))/2.f; // Center to corner
                    if(x==target.size.x/2 && y==target.size.y/2) log(6, st);
                }

                //uv[1] = 1-uv[1];
                uv *= imageSize.x-1;

                if(st[0] < 0 || st[1] < 0 || uv[0] < 0 || uv[1] < 0) continue;
                int is = st[0], it = st[1], iu = uv[0], iv = uv[1]; // Warning: Floors towards zero
                assert_(is >= 0 && it >= 0 && iu >= 0 && iv >= 0);
                if(is >= int(images.size.x)-1 || it >= int(images.size.y)-1) continue;
                if(iu >= int(imageSize.x)-1 || iv >= int(imageSize.y)-1) continue;
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
            //if(find(name, ".png"))
            if(!s.isInteger()) s.until('_');
            //if(!s.isInteger()) continue;
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
            if(!s.isInteger()) s.until('_');
            uint y = uint(s.integer(false));
            s.match('_');
            uint x = uint(s.integer(false));

            for(string mapName: tmp.list(Files)) {
                if(endsWith(mapName, ".png")) continue;
                TextData s (mapName);
                if(!s.isInteger()) s.until('_');
                uint my = uint(s.integer(false));
                if(my != y) continue;
                s.match('_');
                uint mx = uint(s.integer(false));
                if(mx != x) continue;
                if(!s.match(".png.")) s.skip('.');
                uint w = uint(s.integer(false));
                s.match('x');
                uint h = uint(s.integer(false));
                images(x, y) = Image(cast<byte4>(unsafeRef(maps.append(mapName, tmp))), uint2(w, h));
                goto break_;
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
} view;

#endif
