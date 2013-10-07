#include "view.h"
#include "scene.h"
#include "file.h"
#include "gl.h"
#include "matrix.h"
FILE(sponza)

View::View(Scene& scene) : scene(scene),
    simple(sponza(),{"transform white"_}),
    present(sponza(),{"screen present"_}) {
    vertexBuffer.upload<vec2>({vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1)});
    position = (scene.worldMin+scene.worldMax)/2.f;
}

void View::render(int2, int2 size) {
    int w = size.x, h = size.y;

    // Updates view
    mat4 view; view.rotateX(-pitch); view.rotateY(-yaw); // World to view coordinates transform
    // Assumes constant frame rate (60fps)
    velocity = (3.f/4)*velocity + view.inverse()*vec3(strafe*sprint,0,-walk*sprint)+vec3(0,jump*sprint,0); // Friction + Impulse
    position += velocity;
    view.translate( -position );
    mat4 projection; projection.perspective(PI/4, w, h, 1, 32768);

    if(frameBuffer.size() != size) {
        frameBuffer = GLFrameBuffer(w,h,-1);
        resolvedBuffer = GLTexture(w,h);
    }
    frameBuffer.bind(ClearDepth|ClearColor, 0);

    glDepthTest(true); glCullFace(true);
    simple.bind();
    simple["viewProjectionTransform"_] = projection*view;
    for(Surface& surface: scene.surfaces) {
        const Material& material = surface.material;
        simple["diffuseColor"_] = material.diffuse;
        surface.vertexBuffer.bindAttribute(simple, "position"_, 3, offsetof(Vertex, position));
        surface.indexBuffer.draw();
    }

    frameBuffer.blit(resolvedBuffer); // Resolves multisample buffer into resolvedBuffer
    GLFrameBuffer::bindWindow(0, size);
    present.bind();
    present.bindSamplers({"framebuffer"_}); resolvedBuffer.bind(0); //frameBuffer.colorTexture.bind(0);
    vertexBuffer.bindAttribute(present, "position"_, 2);
    vertexBuffer.draw(TriangleStrip);

    if( norm(velocity) > 0.1 ) contentChanged(); // Keeps rendering at 60fps
    frameCount++;
    const float alpha = 0; frameTime = alpha*frameTime + (1-alpha)*(float)time;
    //String status = dec(round(frameTime*1000),3)+"ms "_+ftoa(1/frameTime,1,2)+"fps"_;
    //if(frameCount%32==0) statusChanged(status);
    //if(isPowerOfTwo(frameCount) && frameCount>1) log(dec(frameCount,4), status);
    time.reset();
}

bool View::keyPress(Key key, Modifiers) {
    /**/ if(key=='w') walk++;
    else if(key=='a') strafe--;
    else if(key=='s') walk--;
    else if(key=='d') strafe++;
    else if(key==ControlKey) jump--;
    else if(key==' ') jump++;
    else if(key==ShiftKey) sprint *= 4;
    else if(key=='q') velocity=vec3(0,0,0);
    else if(key=='f') disableShaders=!disableShaders;
    else return false;
    return true; // Starts rendering (if view was still)
}
bool View::keyRelease(Key key, Modifiers) {
    if(key=='w') walk--;
    if(key=='a') strafe++;
    if(key=='s') walk++;
    if(key=='d') strafe--;
    if(key==ControlKey) jump++;
    if(key==' ') jump--;
    if(key==ShiftKey) sprint /= 4;
    return false;
}
bool View::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    setFocus(this);
    if(event==Press && button==LeftButton) { dragStart=cursor; deltaStart=vec2(yaw, pitch); }
    if(event==Motion && button==LeftButton) {
        setDrag(this);
        vec2 delta = deltaStart - float(PI/size.x)*vec2(cursor-dragStart);
        yaw = delta.x; pitch = clip<float>(-PI/2, delta.y, PI/2);
        return true;
    }
    return false;
}

