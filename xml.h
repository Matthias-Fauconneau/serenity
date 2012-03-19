#pragma once
#include "string.h"
#include "map.h"
#include "stream.h"

/// unique pointer to a pool-allocated value with move semantics (useful for recursive types)
template<class T> struct pointer {
    no_copy(pointer)
    static const int poolSize=1<<8;
    static T pool[poolSize];
    static int count;
    explicit pointer(T&& value):value(new (pool+count++) T(move(value))){assert(count<poolSize);}
    pointer(pointer&& o) : value(o.value) { o.value=0; }
    pointer& operator=(pointer&& o) { this->~pointer(); value=o.value; o.value=0; return *this; }
    ~pointer() { if(value) { value->~T(); value=0; } }
    T* value=0;
    const T& operator *() const { return *value; }
    T& operator *() { return *value; }
    const T* operator ->() const { return value; }
    T* operator ->() { return value; }
    explicit operator bool() const { return value; }
    bool operator !() const { return !value; }
    operator const T*() const { return value; }
    operator T*() { return value; }
};
template <class T> T pointer<T>::pool[pointer<T>::poolSize];
template <class T> int pointer<T>::count=0;
template<class T> pointer<T> copy(const pointer<T>& p) { assert(p.value); return pointer<T>(copy(*p.value)); }

/// XML element
struct Element {
    string name,content;
    map< string, string > attributes;
    array< pointer<Element> > children;
    Element(){}
    Element(Element&&)=default;
    Element(TextBuffer& s);
    /// Returns value for \a attribute
    string operator[](const string& attribute) const;
    /// Collects elements with matching \a path
    array<Element> operator()(const string& path) const;
    /// Collects elements with matching \a path
    array<Element> xpath(const string& path) const;
    /// Returns element as parseable string
    string str(const string& prefix=""_) const;
};
template<> Element copy(const Element& e);
template<> string str(const Element& e);

/// Parse an XML document as a tree of \a Element
Element parseXML(array<byte>&& document);
