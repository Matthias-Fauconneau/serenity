#pragma once
#include "widget.h"
#include "gl.h"
#include "function.h"
#include "shader.h"
#include "time.h"
struct Scene;
struct Object;

struct View : Widget {
    View(Scene& scene);

    void render(int2 position, int2 size) override;
    bool keyPress(Key key, Modifiers) override;
    bool keyRelease(Key key, Modifiers) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;

    enum Sort { FrontToBack=0, BackToFront=1 };
    void draw(map<GLShader*,array<Object>>& objects,Sort sort=FrontToBack);

    signal<> contentChanged;
    signal<string> statusChanged;

    Scene& scene;
    Object* selected=0;
    GLFrameBuffer frameBuffer;
    Shader simple {"transform surface"_};
    Shader gamma {"screen gamma"_};

    mat4 projection, inverseProjection, view; vec4 planes[6]; vec3 signs[6];
    vec3 position = 0;
    vec3 velocity = 0;
    float sprint = 16;
    int walk=0, strafe=0, jump=0;
    float yaw = 0, pitch=PI/2;
    int2 dragStart = 0; vec2 deltaStart = 0;
    Time time; float frameTime = 0; uint frameCount=0;
    bool disableShaders=false;
};
