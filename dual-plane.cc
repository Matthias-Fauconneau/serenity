#include "file.h"
#include "parallel.h"
#include "scene.h"
#include "png.h"
#include "interface.h"
#include "text.h"
#include "render.h"
#include "window.h"
#include "view-widget.h"

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

#if 1
struct Render {
    Render() {
        Folder sourceFolder {arguments()[0], home(), false};
        Folder cacheFolder {arguments()[0], tmp, true};
        for(string file: cacheFolder.list(Files)) remove(file, cacheFolder);

        const int N = 33;
        uint2 size (1280, 1024);

        File file(str(N)+'x'+str(N)+'x'+strx(size), cacheFolder, Flags(ReadWrite|Create));
        size_t byteSize = 4*N*N*size.y*size.x*sizeof(half);
        assert_(byteSize <= 16ull*1024*1024*1024);
        file.resize(byteSize);
        Map map (file, Map::Prot(Map::Read|Map::Write));
        mref<half> field = mcast<half>(map);
        log("Reserving", byteSize/1024.f/1024/1024, "G");
        field.clear(); // Explicitly clears to avoid performance skew from clear on page faults (and forces memory allocation)
        log("OK");

        Time time (true);
        //parallel_for(0, N*N, [&](uint unused threadID, size_t stIndex) {
        for(int stIndex: range(N*N)) {
            int sIndex = stIndex%N, tIndex = stIndex/N;

            // Sheared perspective (rectification)
            const float s = sIndex/float(N-1), t = tIndex/float(N-1);
            const unused mat4 M = shearedPerspective(s, t); // FIXME: use to shear images

            //ImageH Z (unsafeRef(field.slice(((0ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH B (unsafeRef(field.slice(((1ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH G (unsafeRef(field.slice(((2ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);
            ImageH R (unsafeRef(field.slice(((3ull*N+tIndex)*N+sIndex)*size.y*size.x, size.y*size.x)), size);

            //scene.render(renderers[threadID], M, (float[]){1,1,1}, Z, B, G, R);
            Image source = decodeImage(Map(str(sIndex-N/2)+","+str(tIndex-N/2)+".png", Folder("unsheared", sourceFolder)));
            assert_(source.size == size);
            // FIXME: shear
            for(size_t y: range(size.y)) for(size_t x: range(size.x)) {
                B(x, y) = source(x, y).b;
                G(x, y) = source(x, y).g;
                R(x, y) = source(x, y).r;
            }
        }//);
        log("Rendered",strx(uint2(N)),"x",strx(size),"images in", time);
    }
} prerender;
#endif

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

struct ViewApp {
    string name;
    vec2 min, max;
    uint2 imageCount;
    uint2 imageSize;
    Map map;
    ref<half> field;

    bool orthographic = false;

    ViewWidget view {uint2(1280,1024), {this, &ViewApp::render}};
    unique<Window> window = nullptr;

    ViewApp() {
        assert_(arguments());
        load(arguments()[0]);
        window = ::window(&view);
        window->setTitle(name);
        window->actions[Key('o')] = [this]{ orthographic=!orthographic; window->render(); };
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
        assert_(imageCount && imageSize);

        if(window) {
            window->setSize();
            window->setTitle(name);
        }
    }
    Image render(uint2 targetSize, vec2 angles) {
        Image target (targetSize);

        mat4 M;
        if(orthographic) {
            M.rotateX(angles.y); // Pitch
            M.rotateY(angles.x); // Yaw
            M.scale(vec3(1,1,-1)); // Z-
        } else {
            // Sheared perspective (rectification)
            const float s = (angles.x+PI/3)/(2*PI/3), t = (angles.y+PI/3)/(2*PI/3);
            M = shearedPerspective(s, t);
        }

        assert_(imageSize.x == imageSize.y && imageCount.x == imageCount.y);

        parallel_chunk(target.size.y, [this, &target, M](uint, size_t start, size_t sizeI) {
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
            assert_(imageSize.x%2==0); // Gather 32bit / half
            const v2si unused sample2D = {    0,           size1/2};
            const v8si unused sample4D = {    0,           size1/2,         size2/2,       (size2+size1)/2,
                                              size3/2, (size3+size1)/2, (size3+size2)/2, (size3+size2+size1)/2};
            //const float scale = (float) imageSize.x / imageCount.x; // st -> uv
            for(size_t targetY: range(start, start+sizeI)) for(size_t targetX: range(target.size.x)) {
                size_t targetIndex = targetY*targetStride + targetX;
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

                if(st[0] < -0 || st[1] < -0) { target[targetIndex]=byte4(0xFF,0,0,0xFF); continue; }
                const int sIndex = st[0], tIndex = st[1];
                if(sIndex >= int(imageCount.x)-1 || tIndex >= int(imageCount.y)-1) { target[targetIndex]=byte4(0,0xFF,0xFF,0xFF); continue; }

                bgr3f S = 0;
#if 0
                if(depthCorrect) {
                    const float z = Z(targetX, targetY);
                    const float z_ = z-1.f/2;

                    const v4sf x = {st[1], st[0]}; // ts
                    const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                    static const v4sf _0011f = {0,0,1,1};
                    const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                    v4sf w01st = __builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0) // ttTT
                            * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1); // sSsS

                    v4sf B, G, R;
                    for(int dt: {0,1}) for(int ds: {0,1}) {
                        vec2 uv_ = uv_uncorrected + scale * (fract(st) - vec2(ds, dt)) * (-z_) / (z_+2);
                        if(uv_[0] < 0 || uv_[1] < 0) { w01st[dt*2+ds] = 0; continue; }
                        int uIndex = uv_[0], vIndex = uv_[1];
                        if(uIndex >= int(imageSize.x)-1 || vIndex >= int(imageSize.y)-1) { w01st[dt*2+ds] = 0; continue; }
                        const size_t base = (size_t)(tIndex+dt)*size3 + (sIndex+ds)*size2 + vIndex*size1 + uIndex;
                        const v2sf x = {uv_[1], uv_[0]}; // vu
                        const v4sf X = __builtin_shufflevector(x, x, 0,1, 0,1);
                        static const v4sf _0011f = {0,0,1,1};
                        const v4sf w_1mw = abs(X - floor(X) - _0011f); // fract(x), 1-fract(x)
                        const v4sf Z = toFloat((v4hf)gather((float*)(fieldZ.data+base), sample2D)); // FIXME
                        const v4sf w01uv = and(__builtin_shufflevector(w_1mw, w_1mw, 2,2,0,0)  // vvVV
                                               * __builtin_shufflevector(w_1mw, w_1mw, 3,1,3,1) // uUuU
                                               , abs(Z - float4(z)) < float4(0x1p-5)); // Discards far samples (tradeoff between edge and anisotropic accuracy)
                        float sum = ::sum(w01uv);
                        const v4sf w01 = float4(1./sum) * w01uv; // Renormalizes uv interpolation (in case of discarded samples)
                        w01st[dt*2+ds] *= sum; // Adjusts weight for st interpolation
                        if(!sum) { B[dt*2+ds] = 0; G[dt*2+ds] = 0; R[dt*2+ds] = 0; continue; }
                        B[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldB.data+base), sample2D)));
                        G[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldG.data+base), sample2D)));
                        R[dt*2+ds] = dot(w01, toFloat((v4hf)gather((float*)(fieldR.data+base), sample2D)));
                    }
                    const v4sf w01 = float4(1./sum(w01st)) * w01st; // Renormalizes st interpolation (in case of discarded samples)
                    const float b = dot(w01, B);
                    const float g = dot(w01, G);
                    const float r = dot(w01, R);
                    S = bgr3f(b, g, r);
                } else
#endif
                {
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
        return target;
    }
} view;
