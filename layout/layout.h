#pragma once
#include "vector.h"
#include "map.h"

static constexpr float inchMM = 25.4;

struct Element {
	vec2 anchor {0}; // Anchors element center to an absolute position (soft constraint)
	float aspectRatio; // Element aspect ratio x/y (initialized by derived class, negative for free ratio)
	vec2 sizeHint {0}; // Size hint
	int2 index {-1}, cellCount {-1}; // Row, column index/size in table
	vec2 min, max; // Element geometry
	vec2 margin, space; // Margin and space of element row/column
	int2 size(float mmPx) const { return int2(round(max*mmPx) - round(min*mmPx)); } // Element size (in pixels)
	virtual ~Element() {}
	virtual struct Image image(float mmPx) const abstract;
};

struct Cell {
	size_t parentElementIndex = -1;
	int2 parentIndex=-1, parentSize = -1;
	bool horizontalExtension = false, verticalExtension = false;
	operator size_t&() { return parentElementIndex; }
};
inline bool operator ==(const Cell& cell, size_t elementIndex) { return cell.parentElementIndex == elementIndex; }

/// Table of elements
/// \note Represented using a grid of element indices
/// \note Elements may extend over multiple cells using extension cells
struct Table {
	union { int2 size = 0; struct { uint columnCount, rowCount; }; };
	buffer<Cell> cells;
	Table() {}
	Table(int2 size) : size(size), cells((size_t)size.y*size.x) { cells.clear(); }
	inline notrace ref<Cell> row(size_t y) const { assert(y<size_t(size.y)); return cells.slice(y*size.x, size.x); }
	inline notrace Cell& operator()(size_t x, size_t y) const { assert(x<size_t(size.x) && y<size_t(size.y), x, y); return cells[y*size.x+x]; }
	inline notrace Cell& operator()(int2 index) const { return operator()(index.x, index.y); }
};

/// Table of elements with layout parameters
struct Layout {
	array<unique<Element>> elements; // Image or text elements
	Table table;
	map<String, String> arguments;
	array<size_t> freeAspects, horizontalAnchors, verticalAnchors, preferredSize;
	bool rowStructure = false, columnStructure = false, gridStructure = true;
	vec2 size = 0, margin = 0, space = 0;
};
