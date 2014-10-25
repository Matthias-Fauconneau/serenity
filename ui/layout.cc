#include "layout.h"
#include "text.h"

// Layout
Graphics Layout::graphics(int2 size) {
    array<Rect> widgets = layout(size);
    Graphics graphics;
    for(uint i: range(count())) {
        graphics.append(at(i).graphics(widgets[i].size), vec2(widgets[i].origin));
        /*graphics.append(Text(str(widgets[i].size())).graphics(widgets[i].size()), vec2(widgets[i].position()));
        graphics.lines << Line{vec2(widgets[i].min), vec2(widgets[i].max)};*/
    }
    return graphics;
}

bool Layout::mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
    array<Rect> widgets = layout(size);
    for(uint i: range(count()))
        if(widgets[i].contains(cursor))
            if(at(i).mouseEvent(cursor-widgets[i].origin,widgets[i].size,event,button,focus))
                return true;
    return false;
}

// Linear
int2 Linear::sizeHint(int2 size) {
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
    uint count = this->count();
    if(!count) return {};
    const int2 size = xy(originalSize);
    int width = size.x /*remaining space*/; int expanding=0, height=0;
    int widths[count], heights[count];

    for(uint i: range(count)) { Widget& child=at(i); assert(*(void**)&child);
        int2 sizeHint = xy(child.sizeHint(originalSize));
        widths[i] = sizeHint.x;
        width -= abs(widths[i]); // Commits minimum width for all widgets
        if(sizeHint.x<0) expanding++; //counts expanding widgets
        height=max(height, heights[i]=(sizeHint.y<0 ? size.y : min(size.y,sizeHint.y))); //necessary height
    }

    int sharing = expanding ?: (main==Share ? count : (main == ShareTight ? count+2 : 0));
    if(sharing && width >= sharing) { // Shares extra space evenly between sharing widgets
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
            int first = max(ref<int>(widths,count)); // First largest size
            int firstCount=0; for(int size: widths) if(size == first) firstCount++; // Counts how many widgets already have the largest size
            assert_(firstCount);
            int second=0; for(int size: widths) if(second<size && size<first) second=size; // Second largest size
            int offset = max(1, min(-width, first-second) / firstCount); // Distributes reduction to all largest widgets (max(1,...) to account for flooring)
            for(int& size: widths) if(size == first) { size -= offset, width += offset; }
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
    else if(main==Center || main==Even || main==Share || main==ShareTight) pen.x+=width/2;
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
		widgets.append( Rect{xy(pen+int2(0,y)), xy(int2(widths[i],heights[i]))} );
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

    int extraWidth;
    /**/  if(uniformX) {
        const int requiredWidth = max(ref<int>(widths,w)) * w;
        const int availableWidth = size.x ?: requiredWidth;
        const int fixedWidth = availableWidth / w;
        for(int& v: widths) v = fixedWidth;
        extraWidth = availableWidth - w*fixedWidth;
    }
    else if(size.x) {
		const int requiredWidth = sum<int>(ref<int>(widths,w));
        extraWidth = size.x ? size.x-requiredWidth: 0;
        const int extra = extraWidth / w; // Extra space per column (may be negative for missing space)
        for(int& v: widths) { v += extra; extraWidth -= extra; } // Distributes extra/missing space
    }
    else extraWidth = 0;

    int extraHeight;
    if(uniformY) {
        const int requiredHeight = max(ref<int>(heights,h)) * h;
        const int availableHeight = size.y ?: requiredHeight;
        const int fixedHeight = availableHeight / h;
        for(int& v: heights) v = fixedHeight;
        extraHeight = availableHeight - w*fixedHeight;
    } else {
        for(uint y : range(h)) {
            int maxY = 0;
            for(uint x: range(w)) {
                uint i = y*w+x;
                if(i<count()) maxY = ::max(maxY, abs(at(i).sizeHint(int2(widths[x],size.y)).y));
            }
            heights[y] = maxY;
        }
		const int requiredHeight = sum<int>(ref<int>(heights,h)); // Remaining space after fixed allocation
        extraHeight = size.y ? size.y-requiredHeight: 0;
        const int extra = extraHeight / h; // Extra space per cell
        if(extra > 0) {
            for(int& v: heights) { v += extra; extraHeight -= extra; } // Distributes extra space
        } else {
            while(extraHeight <= -h) { // While layout is overcommited
                int first = max(ref<int>(heights,h)); // First largest size
                int firstCount=0; for(int size: heights) if(size == first) firstCount++; // Counts how many widgets already have the largest size
                int second=0; for(int size: heights) if(second<size && size<first) second=size; // Second largest size
                int offset = max(1, min(-extraHeight, first-second) / firstCount); // Distributes reduction to all largest widgets
                for(int& size: heights) if(size == first) { size -= offset, extraHeight += offset; }
            }
        }
		assert_(extraHeight > -h, extraHeight, size, "(", ref<int>(widths,w), ")", "(", ref<int>(heights,h),")", sum<int>(ref<int>(heights,h)));
    }

    int Y = extraHeight/2;
    for(uint y : range(h)) {
        int X = extraWidth/2;
        for(uint x: range(w)) {
            uint i = y*w+x;
            if(i<count()) {
				widgets.append( Rect{int2(X,Y), int2(widths[x], heights[y])} );
                X += widths[x];
            }
        }
        Y += heights[y];
        assert_(size.y ==0 || (int2(0) < int2(X,Y) && int2(X,Y) < size+int2(w,h)), X, Y, size, ref<int>(widths,w), ref<int>(heights,h));
    }
    return widgets;
}

int2 GridLayout::sizeHint(int2 size) {
    int2 requiredSize=0;
    for(Rect r: layout(int2(size.x,0))) requiredSize=max(requiredSize, r.origin+r.size);
    return requiredSize;
}
