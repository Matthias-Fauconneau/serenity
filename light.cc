#include "file.h"
#include "image.h"
#include "png.h"
#include "interface.h"
#include "window.h"
#include "raster.h"
#include "parallel.h"

Folder tmp {"/var/tmp/light",currentWorkingDirectory(), true};

static bool intersect(vec3 A, vec3 B, vec3 C, vec3 O, vec3 d, float& t, float& u, float& v) { //from "Fast, Minimum Storage Ray/Triangle Intersection"
    vec3 edge1 = B - A;
    vec3 edge2 = C - A;
    vec3 pvec = cross(d, edge2);
    float det = dot(edge1, pvec);
    if(det < 0) return false;
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
    struct Face { vec3 position[3]; bgr3f color; };
    Face faces[6*2]; // Cube

    Scene() {
        vec3 position[8];
        const float size = 1./2;
        for(int i: range(8)) position[i] = size * (2.f * vec3(i/4, (i/2)%2, i%2) - vec3(1)); // -1, 1
        const int indices[6*4] = { 0,2,3,1, 0,1,5,4, 0,4,6,2, 1,3,7,5, 2,6,7,3, 4,5,7,6};
        const bgr3f colors[6] = {red, green, blue, cyan, magenta, yellow};
        for(int i: range(6)) {
            faces[i*2+0] = {{position[indices[i*4+2]], position[indices[i*4+1]], position[indices[i*4+0]]}, colors[i]};
            faces[i*2+1] = {{position[indices[i*4+3]], position[indices[i*4+2]], position[indices[i*4+0]]}, colors[i]};
        }
    }

    bgr3f raycast(vec3 O, vec3 d) const {
        float nearestZ = inff; bgr3f color (0, 0, 0);
        for(Face face: faces) {
            float t, u, v;
            if(!::intersect(face.position[0], face.position[1], face.position[2], O, d, t, u, v)) continue;
            float z = t*d.z;
            if(z > nearestZ) continue;
            nearestZ = z;
            color = face.color;
        }
        return color;
    }

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

    void render(Image& final, mat4 view) {
        target.setup(int2(final.size));
        pass.setup(target, ref<Face>(faces).size); // Resize if necessary and clears
        for(const Face& face: faces) {
            vec3 A = view*face.position[0], B = view*face.position[1], C = view*face.position[2];
            if(cross(B-A,C-A).z <= 0) continue; // Backward face culling
            vec3 attributes[0];
            pass.submit(A,B,C, attributes, {face.color});
        }
        pass.render(target);
        target.resolve(final);
    }
};

struct Render {
    Render() {
        Scene scene;
        Folder folder {"synthetic", tmp, true};
        for(string file: folder.list(Files)) remove(file, folder);
        const int N = 33;
        Image target (256);
        Time time (true), lastReport(true);
        for(int sIndex: range(N)) {
            if(lastReport.seconds() > 1) { log(sIndex, "/", N, time); lastReport.reset(); }
            for(int tIndex: range(N)) {
                target.clear(0);
                // Sheared perspective (rectification)
                const float S = 2*sIndex/float(N-1)-1, T = 2*tIndex/float(N-1)-1; // [-1, 1]
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
                M.translate(vec3(S,T,0));
                M.translate(vec3(0,0,-1)); // 0 -> -1 (Z-)
                if(0) {
                    parallel_chunk(target.size.y*target.size.x, [&](uint, size_t start, size_t size) { // TODO: SSAA
                        for(int i: range(start, start+size)) {
                            int y = i/target.size.x, x = i%target.size.x;
                            const vec3 O = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, 2.f*y/float(target.size.y-1)-1, 1);
                            const vec3 P = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, 2.f*y/float(target.size.y-1)-1, -1);
                            const vec3 d = normalize(P-O);
                            target(x, y) = byte4(byte3(float(0xFF)*scene.raycast(O, d)), 0xFF);
                        }
                    });
                } else {
                    mat4 NDC;
                    NDC.scale(vec3(vec2(target.size*4u)/2.f, 1)); // 0, 2 -> subsample size
                    NDC.translate(vec3(vec2(1),0.f)); // -1, 1 -> 0, 2
                    scene.render(target, NDC * M);
                }
                writeFile(str(tIndex)+'_'+str(sIndex)+'.'+strx(target.size), cast<byte>(target), folder, true);
                writeFile(str(tIndex)+'_'+str(sIndex)+".png", encodePNG(target), folder, true);
            }
        }
        log(time);
    }
} ;//render;

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
    uint2 imageSize;
    array<Map> maps;
    ImageT<Image> images;
    Scene scene;

    bool sample = false, raycast = false, orthographic = false;

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
    Image render(uint2 size) {
        //if(inputs[view.value] != this->name) load(inputs[view.value]);
        Image target (size);
        target.clear(0);

        mat4 M;
        if(orthographic) {
            M.rotateX(view.viewYawPitch.y); // Pitch
            M.rotateY(view.viewYawPitch.x); // Yaw
            M.scale(vec3(1,1,-1)); // Z-
        } else {
            // Sheared perspective (rectification)
            const float s = (view.viewYawPitch.x+PI/2)/PI, t = (view.viewYawPitch.y+PI/2)/PI;
            const float S = 2*s-1, T = 2*t-1; // [0,1] -> [-1, 1]
            const float left = (-1-S), right = (1-S);
            const float bottom = (-1-T), top = (1-T);
            M(0,0) = 2 / (right-left);
            M(1,1) = 2 / (top-bottom);
            M(0,2) = (right+left) / (right-left);
            M(1,2) = (top+bottom) / (top-bottom);
            const float near = 1-1./2, far = 1+1./2;
            M(2,2) = - (far+near) / (far-near);
            M(2,3) = - 2*far*near / (far-near);
            M(3,2) = - 1;
            M(3,3) = 0;
            M.translate(vec3(S,T,0));
            M.translate(vec3(0,0,-1)); // 0 -> -1 (Z-)
        }

        if(raycast) {
            parallel_chunk(target.size.y*target.size.x, [&](uint, size_t start, size_t size) { // TODO: SSAA
                for(int i: range(start, start+size)) {
                    int y = i/target.size.x, x = i%target.size.x;
                    const vec3 O = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, 2.f*y/float(target.size.y-1)-1, -1);
                    const vec3 P = M.inverse() * vec3(2.f*x/float(target.size.x-1)-1, 2.f*y/float(target.size.y-1)-1, 1);
                    const vec3 d = normalize(P-O);

                    bgr3f S;
                    if(sample) {
                        const vec3 n (0,0,1);
                        const float nd = dot(n, d);
                        assert_(nd);
                        const vec3 n_nd = n / nd;

                        const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,-1)-O) * d.xy();
                        const vec2 ST = (Pst+vec2(1))/2.f;
                        assert_(isNumber(ST), ST, Pst, n_nd, O, d);
                        const vec2 Puv = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
                        const vec2 UV = (Puv+vec2(1))/2.f;

                        //if(name=="synthetic") {
                            //st *= vec2(images.size-uint2(1));
                        const vec2 st = ST * vec2(images.size-uint2(1));
                        /*} else {
                            st[1] = 1-st[1];
                            if(x==target.size.x/2 && y==target.size.y/2) log(1, st);
                            st *= imageSize.y-1; // Pixel units
                            if(x==target.size.x/2 && y==target.size.y/2) log(2, st, imageSize.y);
                            //st += vec2(imageSize)/2.f; // Corner to center
                            //if(x==target.size.x/2 && y==target.size.y/2) log(3, st, min);
                            st = (st-min)/(max-min) * vec2(images.size-uint2(1)); // Pixel to image indices
                            if(x==target.size.x/2 && y==target.size.y/2) log(3, st, min, max, images.size);
                            //st -= vec2(images.size-uint2(1))/2.f; // Center to corner
                            //if(x==target.size.x/2 && y==target.size.y/2) log(5, st);
                        }*/

                        //if(name!="synthetic") uv[1] = 1-uv[1];
                        //uv *= imageSize.x-1;
                        const vec2 uv = UV * float(imageSize.x-1);

                        if(st[0] < 0 || st[1] < 0 || uv[0] < 0 || uv[1] < 0) continue;
                        int is = st[0], it = st[1], iu = uv[0], iv = uv[1]; // Warning: Floors towards zero
                        assert_(is >= 0 && it >= 0 && iu >= 0 && iv >= 0, is, it, iu, iv, st, uv, imageSize, images.size, Pst, Puv, O, d, nd, n_nd);
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

                        S = (1-fs) * Ss[0] + fs * Ss[1];
                    } else {
                        S = float(0xFF)*scene.raycast(O, d);
                    }
                    target(x, y) = byte4(byte3(S), 0xFF);
                }
            });
        } else {
            mat4 NDC;
            NDC.scale(vec3(vec2(target.size*4u)/2.f, 1)); // 0, 2 -> subsample size
            NDC.translate(vec3(vec2(1),0.f)); // -1, 1 -> 0, 2
            scene.render(target, NDC * M);
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
            if(!s.isInteger()) s.until('_');
            int y = s.integer();
            s.skip('_');
            int x = s.integer();

            xRange.start = ::min(xRange.start, x);
            xRange.stop = ::max(xRange.stop, x+1);

            yRange.start = ::min(yRange.start, y);
            yRange.stop = ::max(yRange.stop, y+1);

            if(s.match('_')) {
                float py = s.decimal();
                s.match('_');
                float px = s.decimal();
                min = ::min(min, vec2(px, py));
                max = ::max(max, vec2(px, py));
            }
        }
        if(1) {
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
                s.skip('_');
                uint mx = uint(s.integer(false));
                if(mx != x) continue;

                if(s.match('_')) {
                    s.decimal();
                    s.skip('_');
                    s.decimal();
                    s.skip('_');
                }

                if(!s.match(".png.")) s.skip('.');

                uint w = uint(s.integer(false));
                s.skip('x');
                uint h = uint(s.integer(false));

                assert_(!s);

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
