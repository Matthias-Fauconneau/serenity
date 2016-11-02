#include "scene.h"
#include "parallel.h"

#include "interface.h"
#include "text.h"
#include "render.h"
#include "window.h"

inline string basename(string x) {
    string name = x.contains('/') ? section(x,'/',-2,-1) : x;
    string basename = name.contains('.') ? section(name,'.',0,-2) : name;
    assert_(basename);
    return basename;
}

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
    Scene scene {::parseScene(readFile(basename(arguments()[0])+".scene"))};
    //Scene::Renderer<0> Zrenderer {scene};
    Scene::Renderer<3> BGRrenderer {scene};

    struct ViewWidget : ViewControl, ImageView {
        ViewApp& _this;
        ViewWidget(ViewApp& _this) : _this(_this) {}

        virtual bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& widget) override {
            return ViewControl::mouseEvent(cursor,size,event,button,widget);
        }
        virtual vec2 sizeHint(vec2) override { return vec2(1024); }
        virtual shared<Graphics> graphics(vec2 size) override {
            this->image = _this.render(uint2(size));
            return ImageView::graphics(size);
        }
    } view {*this};
    unique<Window> window = ::window(&view);

    ViewApp() {
        // Fits scene
        vec3 min = inff, max = -inff;
        for(Scene::Face f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        //const float far = scale*(-scene.viewpoint.z+max.z);
        //log(min, max, near, far, far/near);

        // Fits face UV to maximum projected sample rate
        for(Scene::Face& face: scene.faces) { // FIXME: quads
            const vec3 a = face.position[0], b = face.position[1], c = face.position[2], d = face.position[2];
            const vec3 O = (a+b+c+d)/4.f;
            const vec3 N = cross(b-a, c-a);
            assert_(dot(cross(b-a, c-a),cross(c-a, d-a))==0); // Planar quad
            // Viewpoint st with maximum projection
            const vec2 st = clamp(vec2(-1), O.xy() + (O.z/N.z)*N.xy(), vec2(1));
            face.color[0] = (st[0]+1)/2;
            face.color[1] = (st[1]+1)/2;
            // Projects vertices along st view rays on uv plane (perspective)
            const vec2 uvA = st + a.z/near * (a.xy()-st);
            const vec2 uvB = st + b.z/near * (b.xy()-st);
            const vec2 uvC = st + c.z/near * (c.xy()-st);
            const vec2 uvD = st + d.z/near * (d.xy()-st);
            //const float maxU = ::max(length(uvB-uvA), length(uvC-uvD)); // Maximum projected edge length along quad's u axis
            //const float maxV = ::max(length(uvD-uvA), length(uvC-uvB)); // Maximum projected edge length along quad's v axis
            //log(maxU, maxV);
            // Scales uv
            //const float cellSize = 4096; // for visualization
            //for(float& u: face.u) u *= maxU/cellSize;
            //for(float& v: face.v) v *= maxV/cellSize;
            for(float& u: face.u) u *= 2;
            for(float& v: face.v) v *= 2;
        }
    }

    Image render(uint2 targetSize) {
        Image target (targetSize);

        // Fits scene
        vec3 min = inff, max = -inff;
        for(Scene::Face f: scene.faces) for(vec3 p: f.position) { min = ::min(min, p); max = ::max(max, p); }
        max.z += 0x1p-8; // Prevents back and far plane from Z-fighting
        const float scale = 2./::max(max.x-min.x, max.y-min.y);
        const float near = scale*(-scene.viewpoint.z+min.z);
        const float far = scale*(-scene.viewpoint.z+max.z);
        //log(min, max, near, far, far/near);

        mat4 M;
        // Sheared perspective (rectification)
        const float s = (view.viewYawPitch.x+PI/3)/(2*PI/3), t = (view.viewYawPitch.y+PI/3)/(2*PI/3);
        M = shearedPerspective(s, t, near, far);
        M.scale(scale); // Fits scene within -1, 1
        M.translate(-scene.viewpoint);

        ImageH B (target.size), G (target.size), R (target.size);
        scene.render(BGRrenderer, M, (float[]){1,1,1}, {}, B, G, R);
        convert(target, B, G, R);
        return target;
    }
} view;
