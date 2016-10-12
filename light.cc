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

        const size_t N = 17;
        uint2 size = 1024;

        File file(str(N)+'x'+str(N)+'x'+strx(size), folder, Flags(ReadWrite|Create));
        size_t byteSize = 4*N*N*size.y*size.x*sizeof(half);
        assert_(byteSize <= 16ull*1024*1024*1024);
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);
        profile( field.clear() ); // Explicitly clears to avoid performance skew from clear on page faults

        buffer<Scene::Renderer> renderers (threadCount());
        for(Scene::Renderer& renderer: renderers) new (&renderer) Scene::Renderer(scene);

        Time time (true);
        parallel_for(0, N*N, [&](uint threadID, size_t stIndex) {
            int sIndex = stIndex%N, tIndex = stIndex/N;

            // Sheared perspective (rectification)
            const float s = sIndex/float(N-1), t = tIndex/float(N-1);
            mat4 M = shearedPerspective(s, t);

            ImageH Z (unsafeRef(field.slice(((0ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH B (unsafeRef(field.slice(((1ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH G (unsafeRef(field.slice(((2ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH R (unsafeRef(field.slice(((3ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);

            scene.render(renderers[threadID], Z, B, G, R, M);

            ImageH Z01 (Z.size);
            for(size_t i: range(Z.ref::size)) {
                //assert_(Z[i] >= -2 && Z[i] <= 1, (float)Z[i], i%Z.size.x, i/Z.size.x, sIndex, tIndex);
                Z01[i] = (Z[i]+1)/2;
            }
            if(sIndex%16==0 && tIndex%16==0) writeFile(str(sIndex)+'_'+str(tIndex)+".Z.png", encodePNG(convert(Z01, Z01, Z01)), folder, true);
            if(sIndex%16==0 && tIndex%16==0) writeFile(str(sIndex)+'_'+str(tIndex)+".BGR.png", encodePNG(convert(B, G, R)), folder, true);
        });
        log("Rendered",strx(uint2(N)),"x",strx(size),"images in", time);
    }
};//render;

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
    vec2 viewYawPitch = vec2(PI/3, PI/3); // Current view angles

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
    bool sample = true;
    bool raycast = true;
    bool depthCorrect = true;

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
        window->actions[Key('d')] = [this]{ depthCorrect=!depthCorrect; window->render(); };
    }
    Image render(uint2 targetSize) {
        Image target (targetSize);

        mat4 M;
        if(orthographic) {
            M.rotateX(view.viewYawPitch.y); // Pitch
            M.rotateY(view.viewYawPitch.x); // Yaw
            M.scale(vec3(1,1,-1)); // Z-
        } else {
            // Sheared perspective (rectification)
            const float s = (view.viewYawPitch.x+PI/2)/PI, t = (view.viewYawPitch.y+PI/2)/PI;
            //const float sIndex = s*(imageCount.x-1), tIndex = t*(imageCount.y-1);
            //const float s_ = clamp(0, )
            M = shearedPerspective(s, t);
        }

        if(raycast) {
            // FIXME: Z-Pass only
            ImageH Z (target.size);
            if(depthCorrect) scene.render(renderer, Z, M);

            parallel_chunk(target.size.y*target.size.x, [this, &target, M, &Z](uint, size_t start, size_t sizeI) {
                assert_(imageSize.x == imageSize.y && imageCount.x == imageCount.y);
                const float scale = (float) imageSize.x / imageCount.x; // st -> uv
                const int targetStride = target.size.x;
                const int size1 = imageSize.x *1;
                const int size2 = imageSize.y *size1;
                const int size3 = imageCount.x*size2;
                const size_t size4 = (size_t)imageCount.y*size3;
#if 1
                const struct Image4DH : ref<half> {
                    uint4 size;
                    Image4DH(uint2 imageCount, uint2 imageSize, ref<half> data) : ref<half>(data), size(imageCount.y, imageCount.x, imageSize.y, imageSize.x) {}
                    const half& operator ()(uint s, uint t, uint u, uint v) const {
                        assert_(t < size[0] && s < size[1] && v < size[2] && u < size[3], (int)s, (int)t, (int)u, (int)v);
                        size_t index = (((uint64)t*size[1]+s)*size[2]+v)*size[3]+u;
                        assert_(index < ref<half>::size, int(index), ref<half>::size, (int)s, (int)t, (int)u, (int)v, size);
                        return operator[](index);
                    }
                } fieldZ {imageCount, imageSize, field.slice(0*size4, size4)},
                  fieldB {imageCount, imageSize, field.slice(1*size4, size4)},
                  fieldG {imageCount, imageSize, field.slice(2*size4, size4)},
                  fieldR {imageCount, imageSize, field.slice(3*size4, size4)};
#else
                //const ref<half> fieldZ = field.slice(0*size4, size4);
                const ref<half> fieldB = field.slice(1*size4, size4);
                const ref<half> fieldG = field.slice(2*size4, size4);
                const ref<half> fieldR = field.slice(3*size4, size4);
#endif
                assert_(imageSize.x%2==0); // Gather 32bit / half
                const v2si unused sample2D = {    0,           size1/2};
                const v8si unused sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                                  size3/2, (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
                for(size_t targetIndex: range(start, start+sizeI)) {
                    int targetY = targetIndex/targetStride, targetX = targetIndex%targetStride;
                    const vec3 O = M.inverse() * vec3(2.f*targetX/float(targetStride-1)-1, 2.f*targetY/float(target.size.y-1)-1, -1);
                    const vec3 P = M.inverse() * vec3(2.f*targetX/float(targetStride-1)-1, 2.f*targetY/float(target.size.y-1)-1, +1);
                    const vec3 d = normalize(P-O);

                    const vec3 n (0,0,1);
                    const float nd = dot(n, d);
                    const vec3 n_nd = n / nd;

                    const vec2 Pst = O.xy() + dot(n_nd, vec3(0,0,1)-O) * d.xy();
                    const vec2 ST = (Pst+vec2(1))/2.f;
                    const vec2 Puv = O.xy() + dot(n_nd, vec3(0,0,0)-O) * d.xy();
                    const vec2 UV = (Puv+vec2(1))/2.f;

                    const vec2 st = ST * vec2(imageCount-uint2(1));
                    const vec2 uv = UV * vec2(imageSize-uint2(1));

                    if(st[0] < 0 || st[1] < 0 || uv[0] < 0 || uv[1] < 0) { target[targetIndex]=0; continue; }
                    const int sIndex = st[0], tIndex = st[1]; //, uIndex = uv[0], vIndex = uv[1];
                    if(sIndex >= int(imageCount.x)-1 || tIndex >= int(imageCount.y)-1) { target[targetIndex]=0; continue; }
                    bgr3f S = 0;
                    if(depthCorrect) {
                        //v16sf Z = toFloat((v16hf)gather((float*)(fieldZ.data+base), sample4D));
                        //const float z = dot(w01, Z);
                        float z = Z(targetX, targetY);
                        v4sf W[4]; float w = 0;
                        v16hf B, G, R;
                        for(int dt: {0,1}) for(int ds: {0,1}) { // FIXME: SIMD
                            vec2 uv_ = uv + scale * (fract(st) - vec2(ds, dt)) * (-z) / (z+2);
                            if(uv_[0] < 0 || uv_[1] < 0) { target[targetIndex]=0; continue; } // DEBUG
                            //uv_[0] = ::max(0.f, uv_[0]);
                            //uv_[1] = ::max(0.f, uv_[1]);
                            //if(uv_[0] < 0 || uv_[1] < 0) goto discard;
                            int uIndex = uv_[0], vIndex = uv_[1];
                            assert_(uIndex >= 0 && vIndex >= 0, uv_, z);
                            if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { target[targetIndex]=0; continue; } // DEBUG
                            //uIndex = ::min(uIndex, int(imageSize.x)-2);
                            //vIndex = ::min(vIndex, int(imageSize.y)-2);
                            //float sumB = 0, sumG = 0, sumR = 0;
                            //float sumW = 0;
#if 0
                            for(int dv: {0,1}) for(int du: {0,1}) { // FIXME: SIMD
                                if(abs(fieldZ(sIndex+ds, tIndex+dt, uIndex+du, vIndex+dv) - z) > 1./2) continue; // Only close samples
                                float w = (1-abs(fract(uv_[0]) - du)) * (1-abs(fract(uv_[1]) - dv));
                                sumW += w;
                                sumB += w * fieldB(sIndex+ds, tIndex+dt, uIndex+du, vIndex+dv);
                                sumG += w * fieldG(sIndex+ds, tIndex+dt, uIndex+du, vIndex+dv);
                                sumR += w * fieldR(sIndex+ds, tIndex+dt, uIndex+du, vIndex+dv);
                            }
#else
                            const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                            const v2sf x = {uv[1], uv[0]}; // vu
                            const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                            static const v4sf _0011f = {0,0,1,1};
                            const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                            const v4sf w01 = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVVvvVVvvVVvvVV
                                           * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // uUuUuUuUuUuUuUuU
                            const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ.data+base), sample2D));
                            const float z2 = dot(w01, Z);
#if 0
                            if(abs(z2 - z) > 1./2) { W[dt*2+ds] = _0f; continue; } // Only close samples
                            w++;

                            W[dt*2+ds] = w01;
                            ((v4x64&)B)[dt*2+ds] = (b64)gather((float*)(fieldB.data+base), sample2D);
                            ((v4x64&)G)[dt*2+ds] = (b64)gather((float*)(fieldG.data+base), sample2D);
                            ((v4x64&)R)[dt*2+ds] = (b64)gather((float*)(fieldR.data+base), sample2D);
#else
                            w++;
                            W[dt*2+ds] = w01;
                            ((v4x64&)B)[dt*2+ds] = (b64)gather((float*)(fieldB.data+base), sample2D);
                            ((v4x64&)G)[dt*2+ds] = (b64)gather((float*)(fieldG.data+base), sample2D);
                            ((v4x64&)R)[dt*2+ds] = (b64)gather((float*)(fieldR.data+base), sample2D);
#endif
#if 0
                            sumW++;
                            int du=0, dv=0; const float v = fieldZ(sIndex+ds, tIndex+dt, uIndex+du, vIndex+dv) - z;
                            //const float v = z2 - z;
                            sumB += ::max(0.f, -v);
                            sumG += abs(v) > 1./8;
                            sumR += ::max(0.f, v);
#endif
#endif
#if 0
                            float w = (1-abs(fract(st[0]) - ds)) * (1-abs(fract(st[1]) - dt));
                            B += w * sumB/sumW;
                            G += w * sumG/sumW;
                            R += w * sumR/sumW;
#endif
                        }
                        const v4sf x = {st[1], st[0]}; // ts
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        static const v4sf _0011f = {0,0,1,1};
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        const v16sf w01 = shuffle(w_1mw, w_1mw, 2,2,2,2,2,2,2,2, 0,0,0,0,0,0,0,0)  // ttttttttTTTTTTTT
                                        * shuffle(w_1mw, w_1mw, 3,3,3,3,1,1,1,1, 3,3,3,3,1,1,1,1)  // ssssSSSSssssSSSS
                                        * v16sf(float8(W[0],W[1]),float8(W[2],W[3]))
                                        * v16sf(4.f/w); // Scaling (in case of skipped samples)
                        const float b = dot(w01, toFloat(B));
                        const float g = dot(w01, toFloat(G));
                        const float r = dot(w01, toFloat(R));
                        S = bgr3f(b, g, r);
                    } else {
                        const int uIndex = uv[0], vIndex = uv[1];
                        if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { target[targetIndex]=0; continue; }
                        const size_t base = (size_t)tIndex*size3 + sIndex*size2 + vIndex*size1 + uIndex;
                        const v16sf B = toFloat((v16hf)gather((float*)(fieldB.data+base), sample4D));
                        const v16sf G = toFloat((v16hf)gather((float*)(fieldG.data+base), sample4D));
                        const v16sf R = toFloat((v16hf)gather((float*)(fieldR.data+base), sample4D));

                        const v4sf x = {st[1], st[0], uv[1], uv[0]}; // tsvu
                        const v8sf X = __builtin_shufflevector(x, x, 0,1,2,3, 0,1,2,3);
                        static const v8sf _00001111f = {0,0,0,0,1,1,1,1};
                        const v8sf w_1mw = abs(X - floor(X) - _00001111f); // fract(x), 1-fract(x)
                        const v16sf w01 = shuffle(w_1mw, w_1mw, 4,4,4,4,4,4,4,4, 0,0,0,0,0,0,0,0)  // ttttttttTTTTTTTT
                                        * shuffle(w_1mw, w_1mw, 5,5,5,5,1,1,1,1, 5,5,5,5,1,1,1,1)  // ssssSSSSssssSSSS
                                        * shuffle(w_1mw, w_1mw, 6,6,2,2,6,6,2,2, 6,6,2,2,6,6,2,2)  // vvVVvvVVvvVVvvVV
                                        * shuffle(w_1mw, w_1mw, 7,3,7,3,7,3,7,3, 7,3,7,3,7,3,7,3); // uUuUuUuUuUuUuUuU
                        S = bgr3f(dot(w01, B), dot(w01, G), dot(w01, R));
                    }
                    target[targetIndex] = byte4(byte3(float(0xFF)*S), 0xFF);
                }
            });
        } else {
            ImageH Z (target.size), B (target.size), G (target.size), R (target.size);
            scene.render(renderer, Z, B, G, R, M);
            convert(target, B, G, R);
            //for(half& z: Z) z = (z+1)/2; convert(target, Z, Z, Z);
            //window->setTitle(str(::min(apply(Z,[](const half& x){return float(x);})),::max(apply(Z,[](const half& x){return float(x);}))));
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
