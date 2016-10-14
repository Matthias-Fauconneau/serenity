#include "file.h"
#include "parallel.h"
#include "scene.h"
#include "png.h"
#include "interface.h"
#include "text.h"
#include "render.h"
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

        const size_t N = 33;
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
};// renderLightField;

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
    } dragStart {0, 0};

    // Orbital ("turntable") view control
    virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*&) override {
        if(event == Press) {
            dragStart = {cursor, viewYawPitch};
            return true;
        }
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
    bool raycast = false;
    bool depthCorrect = true;

    struct View : ScrollValue, ViewControl, ImageView {
        Light& _this;
        View(Light& _this) : ScrollValue(0, _this.inputs.size-1), _this(_this) {}

        virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
            return ScrollValue::mouseEvent(cursor,size,event,button,widget) || ViewControl::mouseEvent(cursor,size,event,button,widget);
        }
        virtual vec2 sizeHint(vec2) override { return vec2(1024+256+640, 1024); }
        virtual shared<Graphics> graphics(vec2 size) override {
            this->image = _this.render(uint2(/*size*/1024));
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
        target.clear(); // DEBUG

        mat4 M;
        if(orthographic) {
            M.rotateX(view.viewYawPitch.y); // Pitch
            M.rotateY(view.viewYawPitch.x); // Yaw
            M.scale(vec3(1,1,-1)); // Z-
        } else {
            // Sheared perspective (rectification)
            const float s = (view.viewYawPitch.x+PI/3)/(2*PI/3), t = (view.viewYawPitch.y+PI/3)/(2*PI/3);
            //const float sIndex = s*(imageCount.x-1), tIndex = t*(imageCount.y-1);
            //const float s_ = clamp(0, )
            M = shearedPerspective(s, t);
        }

        if(raycast) {
            assert_(imageSize.x == imageSize.y && imageCount.x == imageCount.y);
            const float scale = (float) imageSize.x / imageCount.x; // st -> uv
            const int targetStride = target.size.x;
            const int size1 = imageSize.x *1;
            const int size2 = imageSize.y *size1;
            const int size3 = imageCount.x*size2;
            const size_t size4 = (size_t)imageCount.y*size3;
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

            if(0) {// DEBUG
                const float s = (view.viewYawPitch.x+PI/3)/(2*PI/3), t = (view.viewYawPitch.y+PI/3)/(2*PI/3);
                const uint sIndex = s*(imageCount.x-1), tIndex = t*(imageCount.y-1);
                if(!(sIndex < imageCount.x && tIndex < imageCount.y)) return Image();
                ImageH B (unsafeRef(fieldB.slice(tIndex*size3+sIndex*size2, size3)), imageSize);
                ImageH G (unsafeRef(fieldG.slice(tIndex*size3+sIndex*size2, size3)), imageSize);
                ImageH R (unsafeRef(fieldR.slice(tIndex*size3+sIndex*size2, size3)), imageSize);
                Image BGR (B.size);
                convert(BGR, B, G, R);
                if(target.size!=BGR.size) resize(target, BGR); else target=::move(BGR);
                window->setTitle(str(sIndex)+" "+str(tIndex));
                return target;
            }

            // FIXME: Z-Pass only
            ImageH Z (target.size);
            ImageH WZ (target.size);
            if(depthCorrect) scene.render(renderer, Z, WZ, WZ, WZ, M);

            array<char> debug; Image debugTarget (256+640, 1024); debugTarget.clear();
            //parallel_chunk(target.size.y*target.size.x, [this, &target, M, &Z](uint, size_t start, size_t sizeI) {
            ({ size_t start=0, sizeI=target.size.y*target.size.x;
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

                    const vec2 st = vec2(0x1p-16) + vec2(1-0x1p-16) * ST * vec2(imageCount-uint2(1));
                    const vec2 uv_uncorrected = vec2(1-0x1p-16) * UV * vec2(imageSize-uint2(1));

                    if(st[0] < -0 || st[1] < -0) {
                        assert_(st[0] < -0x1p-16 || st[1] < -0x1p-16, st);
                        target[targetIndex]=byte4(0xFF,0,0,0xFF); continue;
                    }
                    const int sIndex = st[0], tIndex = st[1]; //, uIndex = uv[0], vIndex = uv[1];
                    if(sIndex >= int(imageCount.x)-1 || tIndex >= int(imageCount.y)-1) {
                        assert_(st[0] > int(imageCount.x)-1+0x1p-16 || st[1] > int(imageCount.y)-1+0x1p-16, st);
                        target[targetIndex]=byte4(0,0xFF,0xFF,0xFF); continue;
                    }
                    bgr3f S = 0;
                    if(depthCorrect) {
                        //v16sf Z = toFloat((v16hf)gather((float*)(fieldZ.data+base), sample4D));
                        //const float z = dot(w01, Z);
                        //float z = Z(targetX, targetY);
                        const float wZ = WZ(targetX, targetY);
                        const float z = -2*(wZ*2-1);
                        float w = 0;
                        v4sf B, G, R;
                        float Z2[4]; // DEBUG
                        for(int dt: {0,1}) for(int ds: {0,1}) {
                            //if(z==-1) continue;
                            vec2 uv_ = uv_uncorrected + scale * (fract(st) - vec2(ds, dt)) * (-z) / (z+2);
                            if(uv_[0] < 0 || uv_[1] < 0) { target[targetIndex]=byte4(0xFF,0,0xFF,0xFF); continue; } // DEBUG
                            //uv_[0] = ::max(0.f, uv_[0]);
                            //uv_[1] = ::max(0.f, uv_[1]);
                            //if(uv_[0] < 0 || uv_[1] < 0) goto discard;
                            int uIndex = uv_[0], vIndex = uv_[1];
                            assert_(uIndex >= 0 && vIndex >= 0, uv_, z);
                            if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { target[targetIndex]=byte4(0,0xFF,0,0xFF); continue; } // DEBUG
                            //uIndex = ::min(uIndex, int(imageSize.x)-2);
                            //vIndex = ::min(vIndex, int(imageSize.y)-2);
                            const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                            const v2sf x = {uv_[1], uv_[0]}; // vu
                            const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                            static const v4sf _0011f = {0,0,1,1};
                            const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                            const v4sf w01 = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVV
                                           * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // uUuU
                            //const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ.data+base), sample2D));
                            const v4sf wZ = toFloat((v4hf)gather((float*)(fieldB.data+base), sample2D));
                            const v4sf Z = float4(-2)*(wZ*float4(2)-float4(1));
                            const float z2 = dot(w01, Z);
                            Z2[dt*2+ds] = z2;
#if 1
                            if(abs(z2 - z) > 1./8) continue; // Only close samples
                            w++;

                            B[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldB.data+base), sample2D)));
                            G[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldG.data+base), sample2D)));
                            R[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldR.data+base), sample2D)));
#elif 1

                            w++;
                            B[dt*2+ds] = z2;
                            G[dt*2+ds] = z2;
                            R[dt*2+ds] = z2;
#else
                            w++;
                            const float v = z2 - z;
                            B[dt*2+ds] = ::max(0.f, -v);
                            G[dt*2+ds] = abs(v) > 1./8;
                            R[dt*2+ds] = ::max(0.f, v);
#endif
                        }
                        /*if(!w && uv_uncorrected[0] >= imageSize.x/4 && uv_uncorrected[0] < imageSize.x*3/4 &&
                                 uv_uncorrected[1] >= imageSize.y/4 && uv_uncorrected[1] < imageSize.x*3/4) {*/
                        if(1) if(targetX == view.dragStart.cursor.x && targetY == view.dragStart.cursor.y) {
                            debug.append(str("st", st[0], st[1])+"\n");
                            debug.append(str("uv", uv_uncorrected[0], uv_uncorrected[1])+"\n");
                            debug.append(str("z",  z)+"\n");
                            for(int dt: {0,1}) for(int ds: {0,1}) {
                                vec2 uv_ = uv_uncorrected + scale * (fract(st) - vec2(ds, dt)) * (-z) / (z+2);
                                debug.append(str(ds, dt, (fract(st) - vec2(ds, dt)), (-z) / (z+2), scale * (fract(st) - vec2(ds, dt)) * (-z) / (z+2),
                                                 uv_[0], uv_[1], Z2[dt*2+ds])+"\n");
                                Image target = cropShare(debugTarget, int2(dt*2+ds)*int2(0, 256), uint2(256, 256));
                                for(int y: range(256)) for(int x: range(256)) {
                                    float b = fieldB(sIndex+ds, tIndex+dt, x*(imageSize.x-1)/(target.size.x-1), y*(imageSize.y-1)/(target.size.y-1));
                                    float g = fieldG(sIndex+ds, tIndex+dt, x*(imageSize.x-1)/(target.size.x-1), y*(imageSize.y-1)/(target.size.y-1));
                                    float r = fieldR(sIndex+ds, tIndex+dt, x*(imageSize.x-1)/(target.size.x-1), y*(imageSize.y-1)/(target.size.y-1));
                                    b=g=r=(1+fieldZ(sIndex+ds, tIndex+dt, x*(imageSize.x-1)/(target.size.x-1), y*(imageSize.y-1)/(target.size.y-1)))/2;
                                    S = bgr3f(b, g, r);
                                    target(x, y) = byte4(byte3(float(0xFF)*S), 0xFF);
                                }
                                int tx = round(uv_[0]*(target.size.x-1)/(imageSize.x-1));
                                int ty = round(uv_[1]*(target.size.y-1)/(imageSize.y-1));
                                if(tx >= 1 && ty >= 1 && tx < int(target.size.x-1) && ty < int(target.size.y-1)) {
                                    for(int x: range(tx-1, tx+1 +1))
                                        target(x, ty) = byte4(0,0,0xFF,0xFF);
                                    for(int y: range(ty-1, ty+1 +1))
                                        target(tx, y) = byte4(0,0,0xFF,0xFF);
                                }
                            }
                            Image target = cropShare(debugTarget, int2(256,0), uint2(640));
                            clear(target, byte4(0xFF));
                            auto p = [&target](float x, float y) { return vec2((x+2)/3*(target.size.x-1), y*(target.size.y-1)); };
                            line(target, p(-2, st[1]/(imageCount.y-1)), p(0, uv_uncorrected[1]/(imageSize.y-1)), red);
                            line(target, p(z, 0), p(z, 1));
                            for(const int dt: {0,1}) for(const int ds: {0,1}) {
                                const vec2 uv_ = uv_uncorrected + scale * (fract(st) - vec2(ds, dt)) * (-z) / (z+2);
                                line(target, p(-2, (floor(st[1])+dt)/(imageCount.y-1)), p(0, uv_[1]/(imageSize.y-1)));
                                for(int v: range(imageSize.y-1)) {
                                    const int uIndex = uv_[0];
                                    if(uIndex < 0 || uIndex >= int(imageSize.x)) continue;
                                    const vec2 O(-2, (floor(st[1])+dt)/(imageCount.y-1)); // Origin of viewpoint
                                    const vec2 D0(0, (float)v/(imageSize.y-1)); // Pixel position on UV plane
                                    //float z0 = fieldZ(sIndex+ds, tIndex+dt, uIndex, v);
                                    float wZ0 = fieldB(sIndex+ds, tIndex+dt, uIndex, v)*2-1;
                                    float z0 = -2*wZ0;
                                    const vec2 p0 = O + (D0-O)*((2+z0)/2.f);
                                    const vec2 D1(0, (float)(v+1)/(imageSize.y-1)); // Pixel position on UV plane
                                    //float z1 = fieldZ(sIndex+ds, tIndex+dt, uIndex, v+1);
                                    float wZ1 = fieldB(sIndex+ds, tIndex+dt, uIndex, v+1)*2-1;
                                    float z1 = -2*wZ1;
                                    const vec2 p1 = O + (D1-O)*((2+z1)/2.f);
                                    line(target, p(p0.x, p0.y), p(p1.x, p1.y));
                                }
                            }
                        }
                        const v4sf x = {st[1], st[0]}; // ts
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        static const v4sf _0011f = {0,0,1,1};
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        const v4sf w01 = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0) // ttTT
                                       * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) // sSsS
                                       * float4(4.f/w); // Scaling (in case of skipped samples)
                        const float b = dot(w01, B);
                        const float g = dot(w01, G);
                        const float r = dot(w01, R);
                        S = bgr3f(b, g, r);
                    } else {
                        const int uIndex = uv_uncorrected[0], vIndex = uv_uncorrected[1];
                        if(uv_uncorrected[0] < 0 || uv_uncorrected[1] < 0) { target[targetIndex]=byte4(0,0,0xFF,0xFF); continue; }
                        if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { target[targetIndex]=byte4(0xFF,0xFF,0,0xFF); continue; }
                        const size_t base = (size_t)tIndex*size3 + sIndex*size2 + vIndex*size1 + uIndex;
                        const v16sf B = toFloat((v16hf)gather((float*)(fieldB.data+base), sample4D));
                        const v16sf G = toFloat((v16hf)gather((float*)(fieldG.data+base), sample4D));
                        const v16sf R = toFloat((v16hf)gather((float*)(fieldR.data+base), sample4D));

                        const v4sf x = {st[1], st[0], uv_uncorrected[1], uv_uncorrected[0]}; // tsvu
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
            if(debug) {
                ::render(target, Text(debug, 24, black).graphics(vec2(target.size)));
                assert_(target.size.y == debugTarget.size.y);
                Image target2 (target.size+uint2(debugTarget.size.x, 0));
                copy(cropShare(target2,int2(0),target.size), target);
                copy(cropShare(target2,int2(target.size.x, 0), debugTarget.size), debugTarget);
                target = ::move(target2);
            }
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
