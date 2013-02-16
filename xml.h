#pragma once
/// \file xml.h XML parser
#include "map.h"
#include "data.h"
#include "memory.h"
#include "function.h"

/// Unique reference to an heap allocated value
template<Type T> struct unique {
    no_copy(unique);
    T* pointer;
    unique():pointer(0){}
    template<Type O> unique(unique<O>&& o){pointer=o.pointer; o.pointer=0;}
    template<Type O> unique& operator=(unique<O>&& o){this->~unique(); pointer=o.pointer; o.pointer=0; return *this;}
    /// Instantiates a new value
    template<Type... Args> unique(Args&&... args):pointer(&heap<T>(forward<Args>(args)___)){}
    ~unique() { if(pointer) free(pointer); }
    operator T&() { return *pointer; }
    operator const T&() const { return *pointer; }
    T* operator ->() { return pointer; }
    const T* operator ->() const { return pointer; }
    T* operator &() { return pointer; }
    const T* operator &() const { return pointer; }
    explicit operator bool() { return pointer; }
    bool operator !() const { return !pointer; }
};
template<Type T> string str(const unique<T>& t) { return str(*t.pointer); }

/// XML element providing DOM-like access
struct Element {
    string name, content;
    map< string, string > attributes;
    array< unique<Element> > children; //inline array of heap Element instead of heap array of inline Element (faster resize)
    Element(){}
    /// Creates a content element from \a content
    Element(string&& content):content(move(content)){}
    /// Parses XML data to construct a DOM tree of \a Elements
    /// \note As all name, content and attribute strings are referenced, the input document should live as long as the parsed elements.
    Element(TextData& data, bool html=false);
    explicit operator bool() { return name||content; }
    /// Returns value for \a attribute (fail if missing)
    ref<byte> attribute(const ref<byte>& attribute) const;
    /// Returns value for \a attribute (empty string if missing)
    ref<byte> operator[](const ref<byte>& attribute) const;
    /// Returns child element with tag \a name (fail if missing)
    const Element& child(const ref<byte>& name) const;
    /// Returns child element with tag \a name (empty Element if missing)
    const Element& operator()(const ref<byte>& name) const;
    /// Depth-first visits all descendants
    void visit(const function<void(const Element&)>& visitor) const;
    /// Depth-first visits all descendants, skip element if /a visitor returns false.
    void mayVisit(const function<bool(const Element&)>& visitor) const;
    /// Process elements with matching \a path
    void xpath(const ref<byte>& path, const function<void(const Element&)>& visitor) const;
    /// Collects text content of descendants
    string text() const;
    /// Collects text content of descendants matching path
    string text(const ref<byte>& path) const;
    /// Returns element as parseable string
    string str(uint depth=0) const;
};
string str(const Element& e);

/// Parse an XML document as a tree of \a Element
Element parseXML(const ref<byte>& document);
/// Parse an HTML document as a tree of \a Element
Element parseHTML(const ref<byte>& document);

/// Unescape XML entities
string unescape(const ref<byte>& xml);
