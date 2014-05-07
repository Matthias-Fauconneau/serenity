#include "view.h"
#include "project.h"

bool View::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    int2 delta = cursor-lastPos;
    lastPos = cursor;
    if(event==Press && button==RightButton) renderVolume=!renderVolume;
    if(button && event == Motion) {
        if(renderVolume) {
            rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
            rotation.y = clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
        } else {
            sliceZ = clip(0.f, float(cursor.x)/float(size.x-1), 1.f);
        }
        return true;
    }
    return false;
}

ImageF slice(const VolumeF& volume, uint z) { int3 size = volume.sampleCount; return ImageF(buffer<float>(volume.data.slice(z*size.y*size.x,size.y*size.x)), size.x, size.y); }

void View::render(const Image& target) {
    ImageF image;
    float max = 0; //1; //volume->sampleCount.x/2;
    if(renderVolume) {
        mat4 projection = mat4().rotateX(rotation.y /*Pitch*/).rotateZ(rotation.x /*Yaw*/).scale(float(target.height-1)/float(volume->sampleCount.z-1));
        image = ImageF( target.size() );
        project(image, *volume, Projection(projection, image.size()));
    } else {
        image = slice(*volume, sliceZ*(volume->sampleCount.z-1));
    }
    assert_(target.size() == image.size(), target.size(), image.size());
    convert(target, image, max);
}

float View::sliceZ = 0;
vec2 View::rotation = vec2(PI/4, -PI/3);
