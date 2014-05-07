#include "view.h"
#include "project.h"

bool View::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    int2 delta = cursor-lastPos;
    lastPos = cursor;
    if(!button || event != Motion) return false;
    rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
    rotation.y = clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
    return true;
}

void View::render(const Image& target) {
    mat4 projection = mat4().rotateX(rotation.y /*Pitch*/).rotateZ(rotation.x /*Yaw*/).scale(float(target.height-1)/float(volume->sampleCount.z-1));
    float max = 1; //volume->sampleCount.x/2;
    ImageF linear( target.size() );
    project(linear, *volume, Projection(projection, linear.size()));
    convert(target, linear, max);
}

vec2 View::rotation = vec2(PI/4, -PI/3);
