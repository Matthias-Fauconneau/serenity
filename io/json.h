#pragma once
#include "matrix.h"
#include "variant.h"

inline map<String,String> parseDict(string str) {
    TextData s(str);
    map<String,String> dict;
    for(;s;) {
        string key = s.until('=');
        assert_(key && !dict.contains(key));
        string value = s.until(',');
        dict.insertSorted(copyRef(key), copyRef(value));
    }
    return dict;
}

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
inline Variant parseJSON(const string& str) { TextData s(str); return parseJSON(s); }
