#pragma once
/// \file xml.h XML parser
#include "map.h"
#include "data.h"
#include "function.h"

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
