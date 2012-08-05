#pragma once
#include "string.h"
#include "map.h"
#include "stream.h"
#include "function.h"
#include "memory.h"

/// XML element
struct Element {
    string name; ref<byte> content;
    //array< ref<byte> > content; //explicit content optimization (break interleaved content/Element order)
    map< string, ref<byte> > attributes;
    array< unique<Element> > children; //inline array of Element* instead of heap array of Element (faster reallocation)
    //Element(){}
    /// Creates a content element from \a content
    Element(const ref<byte>& content):content(content){} //TODO: escape analysis
    /// Parses XML stream to construct a DOM tree of \a Elements
    /// \note As all name, content and attribute strings are referenced, the input document should live as long as the parsed elements.
    Element(TextStream& stream, bool html=false); //TODO: escape analysis
    explicit operator bool() { return name||content; }
    /// Returns value for \a attribute (fail if missing)
    ref<byte> at(const ref<byte>& attribute) const;
    /// Returns value for \a attribute (empty string if missing)
    ref<byte> operator[](const ref<byte>& attribute) const;
    /// Returns child element with tag \a name (beware of dangling reference)
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
    string str(const ref<byte>& prefix=""_) const;
};
string str(const Element& e);

/// Parse an XML document as a tree of \a Element
Element parseXML(const ref<byte>& document);
/// Parse an HTML document as a tree of \a Element
Element parseHTML(const ref<byte>& document);

/// Unescape XML entities
string unescape(const ref<byte>& xml);
