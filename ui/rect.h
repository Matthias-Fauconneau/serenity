#pragma once
/// \file rect.h Axis-aligned rectangle with 2D floating point coordinates
#include "vector.h"

struct Rect {
 vec2 min, max;
 explicit Rect(vec2 size) : min(0), max(size) {}
 explicit Rect(vec2 min, vec2 max) : min(min), max(max) {}
 static Rect fromOriginAndSize(vec2 origin, vec2 size) { return Rect(origin, origin+size); }
 vec2 origin() const { return min; }
 vec2 size() const { return max-min; }
 explicit operator bool() { return min<max; }
 bool contains(vec2 p) const { return p>=min && p<=max; }
 void extend(vec2 p) { min=::min(min, p); max=::max(max, p); }
};
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline String str(const Rect& r) { return "["_+str(r.min)+" - "_+str(r.max)+"]"_; }
inline Rect operator +(vec2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
