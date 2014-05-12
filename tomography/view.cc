#include "view.h"

static uint index = 0;
static float maxValue = 0;

bool View::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index = clip(0, int(cursor.x*(projections.size-1)/(size.x-1)), int(projections.size)); return true; }
    return false;
}

int2 View::sizeHint() {
    if(!renderVolume) assert_(2 * volume.sampleCount.xy() == 4 *projections[index].imageSize, volume.sampleCount.xy(), projections[index].imageSize);
    else assert_(index<projections.size, index, projections.size);
    return (renderVolume ? 4 * projections[index].imageSize : 2 * volume.sampleCount.xy());
}

void View::render(const Image& target) {
    ImageF image;
    if(renderVolume) {
        image = ImageF( projections[index].imageSize );
        project(image, volume, projections[index]);
    } else {
        const uint total_num_projections = 5041;
        assert_(volume.sampleCount.z == total_num_projections);
        image = slice(volume, index);
    }
    while(image.size() < target.size()) image = upsample(image);
    assert_(target.size() == image.size(), target.size(), image.size());
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}

bool DiffView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index = clip(0.f, float(cursor.x)/float(size.x-1), 1.f); return true; }
    return false;
}

int2 DiffView::sizeHint() {
    assert_(projections.sampleCount.xy()==sensorSize, projections.sampleCount.xy());
    return projections.sampleCount.xy();
}

void DiffView::render(const Image& target) {
    ImageF reprojection = ImageF( target.size() );
    project(reprojection, volume, Projection(volume.sampleCount, reprojection.size(), index));
    ImageF source = slice(projections, index*(projections.sampleCount.z-1));
    ImageF image = ImageF( target.size() );
    for(uint y: range(image.height)) for(uint x: range(image.width)) image(x,y) = abs(reprojection(x,y)-source(x,y));
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}
