#pragma once
/// \file xml.h XML parser
#include "map.h"
#include "data.h"
#include "function.h"

/// XML element providing DOM-like access
struct Element {
 string name, content;
 map<string, string> attributes;
 array<unique<Element>> children;
 Element(){}
 /// Creates a content element from \a content
 Element(string content) : content(content){}
 /// Parses XML data to construct a DOM tree of \a Elements
 /// \note As all name, content and attribute Strings are referenced, the input document should live as long as the parsed elements.
 Element(TextData& data, bool html=false);
 explicit operator bool() const { return name||content; }
 /// Returns value for \a attribute (fail if missing)
 string attribute(string attribute) const;
 /// Returns value for \a attribute (empty String if missing)
 string operator[](string attribute) const;
 /// Returns child element with tag \a name (fail if missing)
 const Element& child(string name) const;
 /// Returns whether any child element have tag \a name
 bool contains(string name) const;
 /// Returns child element with tag \a name (fail if missing)
 const Element& operator()(string name) const;
 /// Depth-first visits all descendants
 void visit(const function<void(const Element&)>& visitor) const;
 /// Depth-first visits all descendants, skip element if /a visitor returns false.
 void mayVisit(const function<bool(const Element&)>& visitor) const;
 /// Process elements with matching \a path
 void xpath(string path, const function<void(const Element&)>& visitor) const;
 /// Collects text content of descendants
 String text() const;
 /// Collects text content of descendants matching path
 String text(string path) const;
 /// Returns element as parseable String
 String str(uint depth=0) const;
};
String str(const Element& e);

/// Parse an XML document as a tree of \a Element
Element parseXML(string document);
/// Parse an HTML document as a tree of \a Element
Element parseHTML(string document);

/// Unescape XML entities
String unescape(string xml);
