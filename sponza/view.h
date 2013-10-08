#pragma once
#include "function.h"
#include "widget.h"
#include "gl.h"
#include "matrix.h"
#include "time.h"
struct Scene;
struct Surface;

struct View : Widget {
    View(Scene& scene);

    void render(int2 position, int2 size) override;
    void draw(const ref<Surface>& surfaces);
    bool keyPress(Key key, Modifiers) override;
    bool keyRelease(Key key, Modifiers) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;

    signal<> contentChanged;

    Scene& scene;

    GLFrameBuffer frameBuffer;
    GLTexture resolvedBuffer;
    GLShader transform;
    GLShader mask;
    GLShader forward;
    GLShader tangent;
    GLShader sRGB;
    GLVertexBuffer vertexBuffer;

    mat4 light; // light transform
    GLTexture shadow; // shadow map (light depth buffer)


    // Control
    vec3 position = 0;
    vec3 velocity = 0;
    float sprint = 2;
    int walk=0, strafe=0, jump=0;
    float yaw = 0, pitch=0;
    int2 dragStart = 0; vec2 deltaStart = 0;
    // Status
    Time time; float frameTime = 0; uint frameCount=0;
    Surface* selected=0;
};
