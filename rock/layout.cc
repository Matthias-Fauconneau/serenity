#include "layout.h"
#include "display.h"

// Layout
void Layout::render(int2 position, int2 size) {
    push(position+Rect(size));
    array<Rect> widgets = layout(position,size);
    for(uint i: range(count())) {
        push(widgets[i]);
        at(i).render(widgets[i]);
        pop();
    }
    pop();
}

bool Layout::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    array<Rect> widgets = layout(0, size);
    for(uint i: range(count())) if(widgets[i].contains(cursor)) if(at(i).mouseEvent(widgets[i],cursor,event,button)) return true;
    return false;
}

// Linear
int2 Linear::sizeHint() {
    int width=0, expandingWidth=0;
    int height=0, expandingHeight=0;
    for(uint i: range(count())) { Widget& child=at(i); assert(*(void**)&child);
        int2 size=xy(child.sizeHint());
        if(size.y<0) expandingHeight=true;
        height = max(height,abs(size.y));
        if(size.x<0) expandingWidth=true;
        width += abs(size.x);
    }
    return xy(int2((this->expanding||expandingWidth)?-max(1,width):width,expandingHeight?-height:height));
}

array<Rect> Linear::layout(int2 position, int2 size) {
    uint count=this->count();
    if(!count) return {};
    size = xy(size);
    int width = size.x /*remaining space*/; int expanding=0, height=0;
    int widths[count], heights[count];

    for(uint i: range(count)) { Widget& child=at(i); assert(*(void**)&child);
        int2 sizeHint = xy(child.sizeHint());
        width -= abs(widths[i]=sizeHint.x); //commits minimum width for all widgets
        if(sizeHint.x<0) expanding++; //counts expanding widgets
        height=max(height, heights[i]=(sizeHint.y<0 ? size.y : min(size.y,sizeHint.y))); //necessary height
    }

    int sharing = expanding ?: (main==Share? count : 0);
    if(sharing && width >= sharing) { //shares extra space evenly between sharing widgets
        int extra = width/sharing;
        for(uint i: range(count)) {
            if(!expanding || widths[i]<0) { //if all widgets are sharing or this widget is expanding
                widths[i] = abs(widths[i])+extra, width -= extra; //commits extra space
            }
        }
        //width%sharing space remains as extra is rounded down
    } else {
        for(uint i: range(count)) widths[i]=abs(widths[i]); //converts all expanding widgets to fixed
        while(width<=-int(count)) { //while layout is overcommited
            uint best=0; for(uint i: range(count)) if(widths[i]>widths[best]) best=i;
            int& first = widths[best]; //largest size
            int next=0; for(int size: widths) if(size>next && size<first) next=size; //next largest widget size
            int delta = min(-width, first-next);
            if(delta!=0) { first -= delta; width += delta; } //cap size to next largest
            else { int delta=-width/count; for(uint i: range(count)) widths[i]-=delta, width+=delta; } //all widgets already have the same size
        }
    }

    int margin = (main==Spread && count>1) ? width/(count-1) : 0; //spreads any margin between all widgets
    width -= margin*(count-1); //width%(count-1) space remains as margin is rounded down

    if(main==Even) {
        for(uint i: range(count)) widths[i]=size.x/count; //converts all expanding widgets to fixed
        width = size.x-count*size.x/count;
    }

    int2 pen = xy(position);
    if(main==Left) pen.x+=0;
    else if(main==Center || main==Even || main==Spread || main==Share) pen.x+=width/2;
    else if(main==Right) pen.x+=size.x-width;
    else error("");
    if(side==AlignLeft) pen.y+=0;
    else if(side==AlignCenter) pen.y+=(size.y-height)/2;
    else if(side==AlignRight) pen.y+=size.y-height;
    else height=size.y;
    array<Rect> widgets(count);
    for(uint i: range(count)) {
        int y=0;
        if(side==AlignLeft||side==AlignCenter||side==AlignRight||side==Expand) heights[i]=height;
        else if(side==Left) y=0;
        else if(side==Center) y=(height-heights[i])/2;
        else if(side==Right) y=height-heights[i];
        Rect widget = pen+int2(0,y)+Rect(int2(widths[i],heights[i]));
        widgets<< Rect(xy(widget.min),xy(widget.max));
        pen.x += widths[i]+margin;
    }
    return widgets;
}

// Grid
int2 Grid::sizeHint() {
    uint w=width,h=height; for(;;) { if(w*h>=count()) break; if(!width && w<=h) w++; else h++; }
    int2 max(0,0);
    for(uint i: range(count())) max = ::max(max,at(i).sizeHint());
    return int2(w,h)*(max+margin);
}

array<Rect> Grid::layout(int2 position, int2 size) {
    array<Rect> widgets(count());
    if(count()) {
        uint w=width,h=height; for(;;) { if(w*h>=count()) break; if(!width && w<=h) w++; else h++; }
        assert(w && h);
        int2 elementSize = int2(size.x/w,size.y/h);
        int2 margin = (size - int2(w,h)*elementSize) / 2;
        for(uint i=0, y=0;y<h;y++) for(uint x=0;x<w && i<count();x++,i++)  widgets<< position + margin + int2(x,y)*elementSize + Rect(elementSize);
    }
    return widgets;
}
