#include "view.h"
#include "scene.h"
#include "file.h"
#include "gl.h"
#include "matrix.h"
FILE(sponza)

View::View(Scene& scene) : scene(scene),
    transform(sponza(),{"transform"_}),
    mask(sponza(),{"transform mask"_}),
    forward(sponza(),{"transform forward"_}),
    sRGB(sponza(),{"screen sRGB"_}) {
    vertexBuffer.upload<vec2>({vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(1,1)});
    position = (scene.worldMin+scene.worldMax)/2.f;
    {
        GLFrameBuffer shadow(GLTexture(8192, 8192, Depth24|Shadow|Bilinear|Clamp));
        shadow.bind(ClearDepth);
        glDepthTest(true); glCullFace(true);

        // Normalizes light space to [-1,1]
        mat4 normalize;
        normalize.scale(vec3(1,1,-1)); // -> [-1,1]
        normalize.translate(-1); // -> [-1,1]
        normalize.scale(2.f/(scene.lightMax-scene.lightMin)); // -> [0,2]
        normalize.translate(-scene.lightMin); // -> [0,max-min]
        mat4 light = normalize * scene.light;

        transform.bind();
        transform["transform"_] = light;
        for(const Surface& surface: scene.replace) {
            surface.vertexBuffer.bindAttribute(transform, "position", 3, offsetof(Vertex,position));
            surface.indexBuffer.draw();
        }
        mask.bind();
        mask["transform"_] = light;
        mask["maskTexture"_] = 0;
        for(const Surface& surface: scene.blend) {
            surface.material->diffuseTexture.bind(0);
            surface.vertexBuffer.bindAttribute(mask, "position", 3, offsetof(Vertex,position));
            surface.vertexBuffer.bindAttribute(mask, "texCoords", 2, offsetof(Vertex,texCoords));
            surface.indexBuffer.draw();
        }

        // Normalizes from [-1,1] to [0,1] for sampling
        mat4 sampler2D;
        sampler2D.scale(1./2);
        sampler2D.translate(1);
        this->light = sampler2D * light;
        this->shadow = move(shadow.depthTexture);
    }
}

void View::render(int2, int2 size) {
    int w = size.x, h = size.y;

    mat4 view; view.rotateX(-pitch); view.rotateY(-yaw);
    velocity = (3.f/4)*velocity + view.inverse()*vec3(strafe*sprint,0,-walk*sprint)+vec3(0,jump*sprint,0); // Assumes constant frame rate (60fps)
    position += velocity;
    view.translate( -position );
    mat4 projection; projection.perspective(PI/4, w, h, 1, 32768);

    if(frameBuffer.size() != size) {
        frameBuffer = GLFrameBuffer(w,h,-1);
        resolvedBuffer = GLTexture(w,h);
    }
    frameBuffer.bind(ClearDepth|ClearColor);

    forward.bind();
    forward["transform"_] = projection*view;
    forward["lightDirection"_] = normalize(light.inverse().normalMatrix()*vec3(0,0,-1));
    forward["shadowTransform"_] = light;
    forward["shadowMap"_] = 2; shadow.bind(2);
    draw(scene.replace);
    glBlendAlpha(); draw(scene.blend); glBlendNone();

    frameBuffer.blit(resolvedBuffer); // Resolves multisample buffer into resolvedBuffer
    GLFrameBuffer::bindWindow(0, size);
    sRGB.bind();
    sRGB.bindSamplers({"framebuffer"_}); resolvedBuffer.bind(0); //frameBuffer.colorTexture.bind(0);
    vertexBuffer.bindAttribute(sRGB, "position"_, 2);
    vertexBuffer.draw(TriangleStrip);

    if( norm(velocity) > 0.1 ) contentChanged(); // Keeps rendering at 60fps
    frameCount++;
    const float alpha = 0; frameTime = alpha*frameTime + (1-alpha)*(float)time;
    //String status = dec(round(frameTime*1000),3)+"ms "_+ftoa(1/frameTime,1,2)+"fps"_;
    //if(frameCount%32==0) statusChanged(status);
    //if(isPowerOfTwo(frameCount) && frameCount>1) log(dec(frameCount,4), status);
    time.reset();
}
void View::draw(const ref<Surface> &surfaces) {
    for(const Surface& surface: surfaces) {
        const Material& material = surface.material;
        forward["diffuseColor"_] = selected == &surface ? vec3(1,1./2,1./2) : vec3(1);
        forward["diffuseTexture"_] = 0; material.diffuseTexture.bind(0);
        surface.vertexBuffer.bindAttribute(forward, "position"_, 3, offsetof(Vertex, position));
        surface.vertexBuffer.bindAttribute(forward, "texCoords"_, 2, offsetof(Vertex, texCoords));
        surface.vertexBuffer.bindAttribute(forward, "normal"_, 3, offsetof(Vertex, normal));
        surface.indexBuffer.draw();
    }
}

bool View::keyPress(Key key, Modifiers) {
    /**/ if(key=='w') walk++;
    else if(key=='a') strafe--;
    else if(key=='s') walk--;
    else if(key=='d') strafe++;
    else if(key==ControlKey) jump--;
    else if(key==' ') jump++;
    else if(key==ShiftKey) sprint *= 4;
    else if(key=='q') selected=0;
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
static bool intersect( vec3 A, vec3 B, vec3 C, vec3 O, vec3 D, float& t ) { //from "Fast, Minimum Storage Ray/Triangle Intersection"
    if(dot(A-O,D)<=0 && dot(B-O,D)<=0 && dot(C-O,D)<=0) return false;
    vec3 edge1 = B - A;
    vec3 edge2 = C - A;
    vec3 pvec = cross( D, edge2 );
    float det = dot( edge1, pvec );
    if( det < 0.000001 ) return false;
    vec3 tvec = O - A;
    float u = dot(tvec, pvec);
    if(u < 0 || u > det) return false;
    vec3 qvec = cross( tvec, edge1 );
    float v = dot(D, qvec);
    if(v < 0 || u + v > det) return false;
    t = dot(edge2, qvec);
    t /= det;
    return true;
}
static bool intersect(const Surface& surface, vec3 O,vec3 D, float& minZ) {
    bool hit=false;
    for(uint i=0;i<surface.indices.size;i+=3) {
        float z;
        if(::intersect(
                    surface.vertices[surface.indices[i]].position,
                    surface.vertices[surface.indices[i+1]].position,
                    surface.vertices[surface.indices[i+2]].position,O,D,z)) {
            if(z<minZ) { minZ=z; hit=true; }
        }
    }
    return hit;
}
bool View::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    setFocus(this);
    if(event==Press && button==LeftButton) { dragStart=cursor; deltaStart=vec2(yaw, pitch); }
    if(event==Release && button==LeftButton && dragStart==cursor) {
        mat4 transform; transform.perspective(PI/4, size.x, size.y, 1, 32768);
        transform.rotateX(-pitch); transform.rotateY(-yaw);
        vec3 direction = transform.inverse() * normalize(vec3(2.0*cursor.x/size.x-1,1-2.0*cursor.y/size.y,1));
        float minZ=65536; Surface* hit=0;
        for(Surface& surface: scene.replace) if(intersect(surface, position, direction, minZ)) hit = &surface;
        for(Surface& surface: scene.blend) if(intersect(surface, position, direction, minZ)) hit = &surface;
        if(hit) {
            selected=hit;
            log(selected->name, selected->material->name, selected->material->diffusePath, selected->material->maskPath);
        }
        return true;
    }
    if(event==Motion && button==LeftButton) {
        setDrag(this);
        vec2 delta = deltaStart - float(PI/size.x)*vec2(cursor-dragStart);
        yaw = delta.x; pitch = clip<float>(-PI/2, delta.y, PI/2);
        return true;
    }
    return false;
}

