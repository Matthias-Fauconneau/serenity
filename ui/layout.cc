#include "layout.h"

// Layout
Graphics Layout::graphics(int2 size) const {
    array<Rect> widgets = layout(size);
    Graphics graphics;
    for(uint i: range(count())) graphics.append(at(i).graphics(widgets[i].size()), vec2(widgets[i].position()));
    return graphics;
}

bool Layout::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    array<Rect> widgets = layout(size);
    for(uint i: range(count())) if(widgets[i].contains(cursor)) if(at(i).mouseEvent(widgets[i],cursor,event,button)) return true;
    return false;
}

// Linear
int2 Linear::sizeHint(int2 size) const {
    int width=0, expandingWidth=0;
    int height=0, expandingHeight=0;
    for(uint i: range(count())) { Widget& child=at(i); assert(*(void**)&child);
        int2 sizeHint = xy(child.sizeHint(size));
        if(sizeHint .y<0) expandingHeight=true;
        height = max(height,abs(sizeHint .y));
        if(sizeHint .x<0) expandingWidth=true;
        width += abs(sizeHint .x);
    }
    return xy(int2((this->expanding||expandingWidth)?-max(1,width):width,expandingHeight?-height:height));
}

array<Rect> Linear::layout(const int2 originalSize) const {
    uint count=this->count();
    if(!count) return {};
    const int2 size = xy(originalSize);
    int width = size.x /*remaining space*/; int expanding=0, height=0;
    int widths[count], heights[count];

    for(uint i: range(count)) { Widget& child=at(i); assert(*(void**)&child);
        int2 sizeHint = xy(child.sizeHint(originalSize));
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

    int margin = (main==Spread && count>1) ? width/(count-1) : 0; // Spreads any margin between all widgets
    width -= margin*(count-1); //width%(count-1) space remains as margin is rounded down

    if(main==Even) {
        for(uint i: range(count)) widths[i]=size.x/count; //converts all expanding widgets to fixed
        width = size.x-count*size.x/count;
    }

    int2 pen = 0;
    if(main==Spread || main==Left) pen.x+=0;
    else if(main==Center || main==Even || /*main==Spread ||*/ main==Share) pen.x+=width/2;
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
        widgets<< xy(pen+int2(0,y))+Rect(xy(int2(widths[i],heights[i])));
        pen.x += widths[i]+margin;
    }
    return widgets;
}

// Grid
array<Rect> GridLayout::layout(int2 size) const {
    if(!count()) return {};
    array<Rect> widgets(count());
    int w=this->width,h=0/*this->height*/; for(;;) { if(w*h>=(int)count()) break; if(!this->width && w<=h) w++; else h++; }
    int widths[w], heights[h];
    for(uint x: range(w)) {
        int maxX = 0;
        for(uint y : range(h)) {
            uint i = y*w+x;
            if(i<count()) maxX = ::max(maxX, abs(at(i).sizeHint(size).x));
        }
        widths[x] = maxX;
    }
    int availableWidth;
    if(uniformColumnWidths) {
        const int requiredWidth = max(ref<int>(widths,w)) * w;
        availableWidth = size.x ?: requiredWidth;
        const int fixedWidth = availableWidth / w;
        for(int& v: widths) { v = fixedWidth; availableWidth -= v; }
    } else if(size.x) {
        const int requiredWidth = sum(ref<int>(widths,w));
        availableWidth = size.x ?: requiredWidth;
        const int extra = (availableWidth-requiredWidth) / w; // Extra space per column (may be negative for missing space)
        for(int& v: widths) { v += extra; availableWidth -= v; } // Distributes extra/missing space
    }
    for(uint y : range(h)) {
        int maxY = 0;
        for(uint x: range(w)) {
            uint i = y*w+x;
            if(i<count()) maxY = ::max(maxY, abs(at(i).sizeHint(int2(widths[x],size.y)).y));
        }
        heights[y] = maxY;
    }
    const int requiredHeight = sum(ref<int>(heights,h)); // Remaining space after fixed allocation
    int availableHeight = size.y ?: requiredHeight;
    {
        const int extra = (availableHeight-requiredHeight) / h; // Extra space per cell
        for(int& v: heights) { v += extra; availableHeight -= v; } // Distributes extra space
    }
    int Y = availableHeight/2;
    for(uint y : range(h)) {
        int X = availableWidth/2;
        for(uint x: range(w)) {
            uint i = y*w+x;
            if(i<count()) {
                widgets << int2(X,Y) + Rect(int2(widths[x], heights[y]));
                X += widths[x];
            }
        }
        Y += heights[y];
    }
    return widgets;
}

int2 GridLayout::sizeHint(int2 size) const {
    int2 requiredSize=0;
    for(Rect r: layout(int2(size.x,0))) requiredSize=max(requiredSize, r.max);
    return requiredSize;
}
