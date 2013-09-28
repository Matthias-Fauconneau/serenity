#pragma once
#include "widget.h"
#include "gl.h"
#include "function.h"
struct Scene;
struct Object;

struct View : Widget {
    View(Scene& scene);

    void render(int2 position, int2 size) override;
    bool keyPress(Key key, Modifiers) override;
    bool keyRelease(Key key, Modifiers) override;
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;

    void render(/*GLFrameBuffer& deferRender,GLFrameBuffer& targetRender,bool withShadow, bool reverseWinding=false*/);
    enum Sort { FrontToBack=0, BackToFront=1 };
    void draw(map<GLShader*,array<Object>>& objects,Sort sort=FrontToBack);

    signal<> contentChanged;

    Scene& scene;
    Object* selected=0;
    //GLTexture depthBuffer,albedoBuffer,normalBuffer,refractionBuffer,refractionDepthBuffer,reflectionBuffer,lightBuffer,finalBuffer;
    //GLFrameBuffer surfaceRender,refractionSurfaceRender,refractionRender,reflectionRender,lightRender,finalRender;

    //->Camera
    mat4 projection, inverseProjection, view; vec4 planes[6]; vec3 signs[6]; vec4 clipPlane;

    vec3 position = 0;
    vec3 velocity = 0;
    float sprint=2;
    int walk=0, strafe=0, jump=0;
    float yaw = 0, pitch=PI/2;
    int2 dragStart = 0; vec2 deltaStart = 0;
};
