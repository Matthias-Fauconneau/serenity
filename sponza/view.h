#pragma once
#include "function.h"
#include "widget.h"
#include "gl.h"
#include "time.h"
struct Scene;
struct Surface;

struct View : Widget {
    View(Scene& scene);

    void render(int2 position, int2 size) override;
    bool keyPress(Key key, Modifiers) override;
    bool keyRelease(Key key, Modifiers) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;

    signal<> contentChanged;

    Scene& scene;
    GLFrameBuffer frameBuffer;
    GLTexture resolvedBuffer;
    GLShader simple;
    GLShader image; //DEBUG
    GLShader sRGB;
    GLVertexBuffer vertexBuffer;

    vec3 position = 0;
    vec3 velocity = 0;
    float sprint = 1;
    int walk=0, strafe=0, jump=0;
    float yaw = 0, pitch=0;
    int2 dragStart = 0; vec2 deltaStart = 0;
    Time time; float frameTime = 0; uint frameCount=0;
    Surface* selected=0;
};
