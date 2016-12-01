#define RT 1
#include "scene.h"
#include "parallel.h"
#include "png.h"
#include "interface.h"
#include "text.h"
#include "render.h"
#include "window.h"
#include "render.h"
#include "rasterizer.h"
#include "view-widget.h"

struct ViewApp {
    Scene scene {::parseScene(readFile(sceneFile(basename(arguments()[0]))))};
    Render renderer {scene};
    Rasterizer<TextureShader> viewer {scene};

    const uint2 imageSize = 1024;
    bool rasterize = false; // Rasterizes prerendered textures or renders first bounce
    ImageF sumB[2], sumG[2], sumR[2];
    uint count[2] = {0, 0}; // Iteration count (Resets on view angle change)
    vec2 angles = 0;

    ViewWidget view {1024, {this, &ViewApp::render}};
    unique<Window> window = nullptr;

    ViewApp() {
        renderer.clear();
        window = ::window(&view);
        window->actions[Key('r')] = [this]{ rasterize=!rasterize; window->render(); };
    }
    Image render(uint2 targetSize, vec2 angles) {
        Image target (targetSize);

        // Sheared perspective (rectification)
        const float s = angles.x/(PI/3), t = angles.y/(PI/3);
        mat4 M = shearedPerspective(s, t, scene.near, scene.far);
        M.scale(scene.scale); // Fits scene within -1, 1

        if(realTime) {
            ImageH B (target.size), G (target.size), R (target.size);
            renderer.step(); // FIXME: async
            scene.setFaceAttributes(sSize, tSize,(s+1)/2, (t+1)/2);
            ::render(rasterizer, scene, M, (float[]){1,1,1}, {}, B, G, R);
            window->render(); // Accumulates
            assert_(target.size == B.size);
            extern uint8 sRGB_forward[0x1000];
            const float scale = float(0xFFF) / renderer.iterations;
            for(size_t i: range(target.ref::size)) {
                uint b = uint(scale*B[i]);
                uint g = uint(scale*G[i]);
                uint r = uint(scale*R[i]);
                /*b = clamp(0u, b, 0xFFFu); g = clamp(0u, g, 0xFFFu); r = clamp(0u, r, 0xFFFu);*/
                //b = min(b, 0xFFFu); g = min(g, 0xFFFu); r = min(r, 0xFFFu);
                assert_(b >= 0 && b <= 0xFFF, b);
                assert_(g >= 0 && g <= 0xFFF, g);
                assert_(r >= 0 && r <= 0xFFF, r);
                target[i] = byte4(sRGB_forward[b], sRGB_forward[g], sRGB_forward[r], 0xFF);
            }
        } else {
            if(this->angles != angles || sumB[scene.rasterize].size != target.size) {
                this->angles = angles;
                for(uint& v: count) v = 0; // Resets accumulation
            }
            if(count[scene.rasterize] == 0) {
                if(sumB[scene.rasterize].size != target.size) {
                    sumB[scene.rasterize] = ImageF(target.size);
                    sumG[scene.rasterize] = ImageF(target.size);
                    sumR[scene.rasterize] = ImageF(target.size);
                }
                sumB[scene.rasterize].clear(0);
                sumG[scene.rasterize].clear(0);
                sumR[scene.rasterize].clear(0);
            }
            count[scene.rasterize]++;
        const vec3 O = vec3(s,t,0)/scene.scale;
        Random randoms[threadCount()];
        for(Random& random: mref<Random>(randoms,threadCount())) random=Random();
        if(scene.rasterize && count[scene.rasterize]>1 /*Reset only after first first iterations (reuse previous set on new view)*/) {
            {Random random; for(Lookup& lookup: scene.lookups) lookup.generate(random);} // New set of stratified cosine samples for hemispheric rasterizer
        }
        scene.texCount = renderer.scene.texCount = renderer.iterations; // Uses baking for all indirect rays (diffuse, specular)
        scene.setFaceAttributes(sSize, tSize, 1./2, 1./2); // FIXME: TODO: store diffuse (average s,t) texture for non-primary (diffuse) evaluation
        parallel_chunk(target.size.y, [this, &target, O, &randoms](const uint id, const size_t start, const size_t sizeI) {
            const int targetSizeX = target.size.x;
            for(size_t targetY: range(start, start+sizeI)) for(size_t targetX: range(targetSizeX)) {
                size_t targetIndex = targetY*targetSizeX+targetX;
                const vec2 uv = (vec2(targetX, targetY) / vec2(target.size-uint2(1)))*2.f - vec2(1) - O.xy()*scene.scale;
                const vec3 d = normalize(vec3(uv, scene.near));
                bgr3f color = scene.raycast_shade(O, d, randoms[id]);
                sumB[scene.rasterize][targetIndex] += color.b;
                sumG[scene.rasterize][targetIndex] += color.g;
                sumR[scene.rasterize][targetIndex] += color.r;
            }
        });

        extern uint8 sRGB_forward[0x1000];
        for(size_t i: range(target.ref::size)) {
            uint B = uint(sumB[scene.rasterize][i]/count[scene.rasterize]*0xFFF);
            uint G = uint(sumG[scene.rasterize][i]/count[scene.rasterize]*0xFFF);
            uint R = uint(sumR[scene.rasterize][i]/count[scene.rasterize]*0xFFF);
            B = clamp(0u, B, 0xFFFu);
            G = clamp(0u, G, 0xFFFu);
            R = clamp(0u, R, 0xFFFu);
            assert_(B >= 0 && B <= 0xFFF, B);
            assert_(G >= 0 && G <= 0xFFF, G);
            assert_(R >= 0 && R <= 0xFFF, R);
            target[i] = byte4(sRGB_forward[B], sRGB_forward[G], sRGB_forward[R], 0xFF);
        }
        window->render(); // Accumulates
        return target;
    }
} view;
