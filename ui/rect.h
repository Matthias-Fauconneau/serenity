#pragma once

/// Axis-aligned rectangle
struct Rect {
    int2 min,max;
    explicit Rect(int2 max):min(0,0),max(max){}
    Rect(int x, int y):min(0,0),max(x,y){}
    Rect(int2 min, int2 max):min(min),max(max){}
    bool contains(int2 p) { return p>=min && p<max; }
    bool contains(Rect r) { return r.min>=min && r.max<=max; }
    explicit operator bool() { return (max-min)>int2(0,0); }
    int2& position() { return min; }
    int2 size() { return max-min; }
};
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
inline Rect operator |(Rect a, Rect b) { return Rect(min(a.min,b.min),max(a.max,b.max)); }
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline bool operator ==(Rect a, Rect b) { return a.min==b.min && a.max==b.max; }
//inline String str(const Rect& r) { return "Rect("_+str(r.min)+" - "_+str(r.max)+")"_; }
