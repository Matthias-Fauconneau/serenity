#include "layout.h"
#include "display.h"

#include "array.cc"
template struct array<Widget*>;

/// Layout

bool Layout::mouseEvent(int2 position, Event event, Button button) {
    for(uint i=0;i<count();i++) { Widget& child=at(i);
        if(position >= child.position && position <= child.position+child.size) {
            if(child.mouseEvent(position-child.position,event,button)) return true;
        }
    }
    return false;
}

void Layout::render(int2 parent) {
    push(Rect(parent+position,parent+position+size));
    for(uint i=0;i<count();i++) at(i).render(parent+position);
    pop();
}

/// Widgets

uint Widgets::count() const { return array::size(); }
Widget& Widgets::at(int i) { return *array::at(i); }

/// Linear

int2 Linear::sizeHint() {
    int width=0, expandingWidth=0;
    int height=0, expandingHeight=0;
    for(uint i=0;i<count();i++) { Widget& child=at(i); assert(*(void**)&child, str());
        int2 size=xy(child.sizeHint());
        if(size.y<0) expandingHeight=true;
        height = max(height,abs(size.y));
        if(size.x<0) expandingWidth=true;
        width += abs(size.x);
    }
    return xy(int2((this->expanding||expandingWidth)?-max(1,width):width,expandingHeight?-height:height));
}

/// Sets the array size to \a size, filling with \a value
template<class T> inline void fill(array<T>& a, const T& value, int size) {
    a.reserve(size); a.setSize(size);
    for(int i=0;i<size;i++) new (&a[i]) T(copy(value));
}

void Linear::update() {
    if(!count()) return;
    int2 size = xy(this->size);
    int width = size.x /*remaining space*/, sharing=0 /*expanding count*/, expandingWidth=0, height=0;
    array<int> hints; fill(hints,-1,count()); array<int> sizes; fill(sizes,-1,count());
    //allocate fixed space and convert to expanding if not enough space
    for(uint i=0;i<count();i++) { Widget& child=at(i); assert(*(void**)&child, str());
        int2 sizeHint = xy(child.sizeHint());
        int hint = sizeHint.x; if(abs(sizeHint.y)>abs(height)) height=sizeHint.y;
        if(hint >= width) hint = -hint; //convert to expanding if not enough space
        if(hint >= 0) width -= (sizes[i]=hints[i]=hint); //allocate fixed size
        else {
            hints[i] = -hint;
            sharing++; //count expanding widgets
            expandingWidth += -hint; //compute minimum total expanding size
        }
    }
    width -= expandingWidth;
    bool expanding=sharing;
    if(!expanding) sharing=count(); //if no expanding: all widgets will share the extra space
    if(width > 0 || sharing==1) { //share extra space evenly between expanding/all widgets
        int extra = uint(width)/uint(sharing+!expanding); //if no expanding: keep space for margins
        for(uint i=0;i<count();i++) {
            sizes[i] = hints[i] + ((!expanding || sizes[i]<0)?extra:0);
        }
        width -= extra*sharing; //remaining margin due to integer rounding
    } else { //reduce biggest widgets first until all fit
        for(uint i=0;i<count();i++) if(sizes[i]<0) sizes[i]=hints[i]; //allocate all widgets
        while(width<-sharing) {
            int& first = max(sizes);
            int second=first; for(int e: sizes) if(e>second && e<first) second=e;
            int delta = first-second;
            if(delta==0 || delta>int(uint(-width)/uint(sharing))) delta=uint(-width)/uint(sharing); //if no change or bigger than necessary
            first -= delta; width += delta;
        }
    }
    int2 pen = int2(width/2,0); //external margin
    if(align>=0) {
        pen.y=size.y-min(abs(height),size.y); //align right/bottom
        if(align==0) pen.y/=2; //align center
    } //else align top/left
    for(uint i=0;i<count();i++) {
        at(i).size = xy(int2(sizes[i],min(abs(height),size.y)));
        at(i).position = xy(pen);
        at(i).update();
        pen.x += sizes[i];
    }
}

/// UniformGrid

int2 UniformGrid::sizeHint() {
    uint w=width,h=height; for(;;) { if(w*h>=count()) break; if(w<=h) w++; else  h++; }

    int2 max(0,0);
    for(uint i=0;i<count();i++) {
        int2 size=at(i).sizeHint();
        max = ::max(max,size);
    }
    return int2(-w,-h)*max; //always expanding
}

void UniformGrid::update() {
    uint w=width,h=height; for(;;) { if(w*h>=count()) break; if(w<=h) w++; else  h++; }

    int2 size(Widget::size.x/w,  Widget::size.y/h);
    int2 margin = (Widget::size - int2(w,h)*size) / 2;
    uint i=0; for(uint y=0;y<h;y++) for(uint x=0;x<w;x++,i++) { if(i>=count()) return; Widget& child=at(i);
        child.position = margin + int2(x,y)*size; child.size=size;
    }
}
