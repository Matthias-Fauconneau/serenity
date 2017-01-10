#pragma once
#include "matrix.h"
#include "variant.h"

inline Variant parseJSON(TextData& s) {
    s.whileAny(" \t\n\r"_);
    if(s.match("true")) return true;
    else if(s.match("false")) return false;
    else if(s.match('"')) {
        return copyRef(s.until('"'));
    }
    else if(s.match('{')) {
        Dict dict;
        s.whileAny(" \t\n\r"_);
        if(!s.match('}')) for(;;) {
            s.skip('"');
            string key = s.until('"');
            assert_(key && !dict.contains(key));
            s.whileAny(" \t\n\r"_);
            s.skip(':');
            Variant value = parseJSON(s);
            dict.insertSorted(copyRef(key), ::move(value));
            s.whileAny(" \t\n\r"_);
            if(s.match(',')) { s.whileAny(" \t\n\r"_); continue; }
            if(s.match('}')) break;
            error("Expected , or }"_);
        }
        return dict;
    }
    else if(s.match('[')) {
        array<Variant> list;
        s.whileAny(" \t\n\r"_);
        if(!s.match(']')) for(;;) {
            Variant value = parseJSON(s);
            list.append( ::move(value) );
            s.whileAny(" \t\n\r"_);
            if(s.match(',')) continue;
            if(s.match(']')) break;
            error("Expected , or ]"_);
        }
        return list;
    }
    else {
        string d = s.whileDecimal();
        if(d) return parseDecimal(d);
        else error("Unexpected"_, s.peek(16));
    }
}

inline const vec2 Vec2(ref<Variant> v) { return vec2((float)v[0],(float)v[1]); }
inline const vec3 Vec3(ref<Variant> v) { return vec3((float)v[0],(float)v[1],(float)v[2]); }

static const mat4 transform(const Dict& object) {
    const Variant& transform = object.at("transform");
    mat4 M;
    if(transform.type == Variant::Dict) {
        const Dict& t = transform;
        const vec3 look_at = Vec3(t.at("look_at"));
        const vec3 position = Vec3(t.at("position"));
        const vec3 z = normalize(look_at - position);
        vec3 y = Vec3(t.at("up"));
        y = normalize(y - dot(y,z)*z); // Projects up on plane orthogonal to z
        const vec3 x = cross(y, z);
        M[0] = vec4(x, 0);
        M[1] = vec4(y, 0);
        M[2] = vec4(z, 0);
        M[3].xyz() = position/2.f;
        M.scale(8);
    } else {
        assert_(transform.type == Variant::List);
        for(uint i: range(16)) M(i/4, i%4) = transform.list[i];
    }
    return M;
}

static inline mat4 parseCamera(ref<byte> file) {
    TextData s (file);
    Variant root = parseJSON(s);
    const Dict& camera = root.dict.at("camera");
    return ::transform( camera );
}
