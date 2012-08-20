#include "layout.h"
#include "display.h"

/// Layout

void Layout::render(int2 position, int2 size) {
    push(position+Rect(size));
    array<Rect> widgets = layout(position,size);
    for(uint i=0;i<count();i++) at(i).render(widgets[i]);
    pop();
}

bool Layout::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    array<Rect> widgets = layout(int2(0,0), size);
    for(uint i=0;i<count();i++) if(widgets[i].contains(cursor)) if(at(i).mouseEvent(widgets[i],cursor,event,button)) return true;
    return false;
}

/// Linear

int2 Linear::sizeHint() {
    int width=0, expandingWidth=0;
    int height=0, expandingHeight=0;
    for(uint i=0;i<count();i++) { Widget& child=at(i); assert(*(void**)&child);
        int2 size=xy(child.sizeHint());
        if(size.y<0) expandingHeight=true;
        height = max(height,abs(size.y));
        if(size.x<0) expandingWidth=true;
        width += abs(size.x);
    }
    return xy(int2((this->expanding||expandingWidth)?-max(1,width):width,expandingHeight?-height:height));
}

/// Sets the array size to \a size, filling with \a value
template<class T> void fill(array<T>& a, const T& value, int size) {
    a.reserve(size); a.setSize(size);
    for(int i=0;i<size;i++) new (&a[i]) T(copy(value));
}

array<Rect> Linear::layout(int2 position, int2 size) {
    if(!count()) return __();
    size = xy(size);
    int width = size.x /*remaining space*/; int expanding=0, height=0;
    int sizes[count()];

    for(uint i=0;i<count();i++) { Widget& child=at(i); assert(*(void**)&child);
        int2 sizeHint = xy(child.sizeHint());
        width -= abs(sizes[i]=sizeHint.x); //commits minimum width for all widgets
        if(sizeHint.x<0) expanding++; //counts expanding widgets
        height=max(height, sizeHint.y<0 ? size.y : sizeHint.y); //necessary height
    }

    int sharing = expanding ?: (main==Share? count() : 0);
    if(sharing && width >= sharing) { //shares extra space evenly between sharing widgets
        int extra = width/sharing;
        for(uint i=0;i<count();i++) {
            if(!expanding || sizes[i]<0) //if all widgets are sharing or this widget is expanding
                sizes[i] = abs(sizes[i])+extra; width -= extra; //commits extra space
        }
        //width%sharing margin remains as extra is truncated
    } else {
        for(uint i=0;i<count();i++) sizes[i]=abs(sizes[i]); //converts all expanding widgets to fixed
        while(width<=-int(count())) { //while layout is overcommited
            uint best=0; for(uint i=0;i<count();i++) if(sizes[i]>sizes[best]) best=i;
            int& first = sizes[best]; //largest size
            int next=0; for(int size: sizes) if(size>next && size<first) next=size; //next largest widget size
            int delta = min(-width, first-next);
            if(delta!=0) { first -= delta; width += delta; } //cap size to next largest
            else { int delta=-width/count(); for(uint i=0;i<count();i++) sizes[i]-=delta, width+=delta; } //all widgets already have the same size
        }
    }

    int2 pen = position;
    if(main==Left) pen.x+=0;
    else if(main==Center) pen.x+=(size.x-width)/2;
    else if(main==Right) pen.x+=size.x-width;
    if(side==Left) pen.y+=0;
    else if(side==Center) pen.y+=(size.y-height)/2;
    else if(side==Right) pen.y+=size.y-height;
    array<Rect> widgets(count());
    for(uint i=0;i<count();i++) {
        widgets<< xy(pen)+Rect(xy(int2(sizes[i],height)));
        pen.x += sizes[i];
    }
    return widgets;
}

/// UniformGrid

int2 UniformGrid::sizeHint() {
    uint w=width,h=height; for(;;) { if(w*h>=count()) break; if(w<=h) w++; else  h++; }
    int2 max(0,0);
    for(uint i=0;i<count();i++) {
        int2 size=at(i).sizeHint();
        max = ::max(max,size);
    }
    return int2(w,h)*max; //fixed size
}

array<Rect> UniformGrid::layout(int2 position, int2 size) {
    uint w=width,h=height; for(;;) { if(w*h>=count()) break; if(w<=h) w++; else  h++; }
    int2 elementSize = int2(size.x/w,size.y/h);
    int2 margin = (size - int2(w,h)*elementSize) / 2;
    array<Rect> widgets(count());
    uint i=0; for(uint y=0;y<h;y++) for(uint x=0;x<w;x++,i++) { if(i>=count()) return widgets;
        widgets<< position + margin + int2(x,y)*size + Rect(elementSize);
    }
    return widgets;
}
