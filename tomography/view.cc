#include "view.h"
#include "project.h"

static float index = 0;
static float maxValue = 0;

bool View::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index = clip(0.f, float(cursor.x)/float(size.x-1), 1.f); return true; }
    return false;
}

int2 View::sizeHint() {
    if(!renderVolume) assert_(volume->sampleCount.xy()==sensorSize);
    return renderVolume ? sensorSize : volume->sampleCount.xy();
}

void View::render(const Image& target) {
    ImageF image;
    if(renderVolume) {
        image = ImageF( target.size() );
        project(image, *volume, Projection(volume->sampleCount, image.size(), index*total_num_projections));
    } else {
        const uint total_num_projections = 5041;
        assert_(volume->sampleCount.z == total_num_projections);
        image = slice(*volume, index*(volume->sampleCount.z-1));
    }
    assert_(target.size() >= image.size(), target.size(), image.size());
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}

bool DiffView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index = clip(0.f, float(cursor.x)/float(size.x-1), 1.f); return true; }
    return false;
}

int2 DiffView::sizeHint() {
    assert_(projections->sampleCount.xy()==sensorSize, projections->sampleCount.xy());
    return projections->sampleCount.xy();
}

void DiffView::render(const Image& target) {
    ImageF reprojection = ImageF( target.size() );
    project(reprojection, *volume, Projection(volume->sampleCount, reprojection.size(), index));
    ImageF source = slice(*projections, index*(projections->sampleCount.z-1));
    ImageF image = ImageF( target.size() );
    for(uint y: range(image.height)) for(uint x: range(image.width)) image(x,y) = abs(reprojection(x,y)-source(x,y));
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}
