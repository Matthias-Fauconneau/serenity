#pragma once
#include "widget.h"
#include "gl.h"
struct Scene;
struct Object;

struct View : Widget {
    View(Scene& scene);
    void render(int2 position, int2 size);
    void render(/*GLFrameBuffer& deferRender,GLFrameBuffer& targetRender,bool withShadow, bool reverseWinding=false*/);
    enum Sort { FrontToBack=0, BackToFront=1 };
    void draw(map<GLShader*,array<Object>>& objects,Sort sort=FrontToBack);

    Scene& scene;

    //GLTexture depthBuffer,albedoBuffer,normalBuffer,refractionBuffer,refractionDepthBuffer,reflectionBuffer,lightBuffer,finalBuffer;
    //GLFrameBuffer surfaceRender,refractionSurfaceRender,refractionRender,reflectionRender,lightRender,finalRender;

    //->Camera
    mat4 projection, inverseProjection, view; vec4 planes[6]; vec3 signs[6]; vec4 clipPlane;

    vec3 position;
    vec3 velocity;
    vec3 momentum;
    float speed=2;
    int walk=0,strafe=0,jump=0;
    float pitch=PI/2, yaw=0;
    //QPoint drag; bool grab = 0;
};
