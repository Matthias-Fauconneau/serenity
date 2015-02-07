#include "solve.h"
#include "algebra.h"

LayoutSolve::LayoutSolve(Layout&& _this) : Layout(move(_this)) {
	// -- Unknowns
	size_t unknownIndex = 0;

	size_t uniformMargin = unknownIndex; unknownIndex += 2; log(2, "uniform margin");
	size_t uniformSpace = unknownIndex; unknownIndex += 2; log(2, "uniform space");

	size_t rowMargins = unknownIndex; unknownIndex += table.rowCount; log(table.rowCount, "row margins");
	size_t columnMargins = unknownIndex; unknownIndex += table.columnCount; log(table.columnCount, "column margins");

	size_t rowSpaces = unknownIndex; unknownIndex += table.rowCount; log(table.rowCount, "row spaces");
	size_t columnSpaces = unknownIndex; unknownIndex += table.columnCount; log(table.columnCount, "column spaces");

	size_t regularizedUnknownCount = unknownIndex;

	size_t columnWidths = unknownIndex; unknownIndex += table.columnCount; log(table.columnCount, "column widths");
	size_t rowHeights = unknownIndex; unknownIndex += table.rowCount; log(table.rowCount, "row heights");

	size_t elementHeights = unknownIndex; unknownIndex += elements.size; log(elements.size, "element heights");
	size_t elementsWidths = unknownIndex; unknownIndex += freeAspects.size; if(freeAspects) log(freeAspects.size, "element widths");

	const size_t unknownCount = unknownIndex;
	log("=", unknownCount, "unknowns");

	// -- Constraints
	struct Constraint : buffer<float>{
		float constant = 0;
		Constraint(size_t size) : buffer(size) { clear(0); }
		float& operator[](int unknownIndex) { assert(at(unknownIndex)==0); return at(unknownIndex); }
		float operator[](int unknownIndex) const { return at(unknownIndex); }
		void operator*=(float weight) { for(float& x: *this) x*=weight; constant*=weight; }
	};
	// Sets height coefficient
	auto setHeightCoefficient = [=](Constraint& constraint, size_t elementIndex, float coefficient=1) {
		constraint[elementHeights+elementIndex] = coefficient;
	};
	// Sets width coefficient handling either fixed or free aspect ratio transparently
	auto setWidthCoefficient = [=](Constraint& constraint, size_t elementIndex, float coefficient=1) {
		size_t freeElementWidth = freeAspects.indexOf(elementIndex);
		if(freeElementWidth == invalid)
			constraint[elementHeights+elementIndex] = elements[elementIndex]->aspectRatio * coefficient; // ratio · height
		else
			constraint[elementsWidths+freeElementWidth] = 1 * coefficient; // free width
	};
	array<Constraint> constraints;
	auto constraint = [&]() -> Constraint& { return constraints.append(unknownCount); };

	// • Fits column widths
	for(size_t rowIndex: range(table.rowCount)) {
		Constraint& fitColumnWidths = constraint();  // sum(widths) + 2·margin = width
		for(const size_t columnIndex : range(table.columnCount)) fitColumnWidths[columnWidths+columnIndex] = 1;
		fitColumnWidths[uniformMargin+0] = 2; // Uniform margin x
		fitColumnWidths[rowMargins+rowIndex] = 2; // Row margin x
		fitColumnWidths.constant = size.x -table.columnCount*size.x/table.columnCount -2*margin.x;
	}

	// • Fits row heights
	for(size_t columnIndex: range(table.columnCount)) {
		Constraint& fitRowHeights = constraint(); // sum(heights) + 2·margin = height
		for(const size_t rowIndex : range(table.rowCount)) fitRowHeights[rowHeights+rowIndex] = 1;
		fitRowHeights[uniformMargin+1] = 2; // Uniform margin y
		fitRowHeights[columnMargins+columnIndex] = 2; // Column margin y
		fitRowHeights.constant = size.y -table.rowCount*size.y/table.rowCount -2*margin.y;
	}

	// • Fits elements to their columns/rows
	for(size_t elementIndex : range(elements.size)) {
		const Element& element = elements[elementIndex];
		{  Constraint& fitElementWidth = constraint();  // element width + 2·column space = columns width
			setWidthCoefficient(fitElementWidth, elementIndex); // Element width
			fitElementWidth[uniformSpace+0] = 2; // Uniform space x
			if(element.cellCount.x == 1) {
				fitElementWidth[columnSpaces+element.index.x] = 2; // Column space
				fitElementWidth[columnWidths+element.index.x] = -1; // Column width
			} else {
				fitElementWidth[columnSpaces+element.index.x] = 1; // Column space
				for(size_t columnIndex: range(element.index.x, element.index.x+element.cellCount.x))
					fitElementWidth[columnWidths+columnIndex] = -1; // Column width
				fitElementWidth[columnSpaces+element.index.x+element.cellCount.x-1] = 1; // Column space
			}
			fitElementWidth.constant = element.cellCount.x*size.x/table.columnCount -2*space.x;
		}
		{  Constraint& fitElementHeight = constraint();  // Element height + 2·space = rows height
			setHeightCoefficient(fitElementHeight, elementIndex); // Element height
			fitElementHeight[uniformSpace+1] = 2; // Uniform space y
			if(element.cellCount.y == 1) {
				fitElementHeight[columnSpaces+element.index.y] = 2; // Row space
				fitElementHeight[rowHeights+element.index.y] = -1; // Row height
			} else {
				fitElementHeight[rowSpaces+element.index.y] = 1; // Row space
				for(size_t rowIndex: range(element.index.y, element.index.y+element.cellCount.y))
					fitElementHeight[rowHeights+rowIndex] = -1; // Row height
				fitElementHeight[rowSpaces+element.index.y+element.cellCount.y-1] = 1; // Row space
			}
			fitElementHeight.constant = element.cellCount.y*size.y/table.rowCount -2*space.y;
		}
	}

	// • Horizontal anchors
	for(size_t elementIndex: horizontalAnchors) {
		for(const size_t rowIndex : range(table.rowCount)) {
			if(!table.row(rowIndex).contains(elementIndex)) continue;
			log("Anchor", rowIndex, elements[elementIndex]->anchor.x);
			Constraint& anchor = constraint();
			anchor[uniformMargin+0] = 1; // Uniform margin x
			anchor[rowMargins+rowIndex] = 1; // Row margin x
			int2 index = elements[elementIndex]->index;
			for(const size_t columnIndex : range(index.x)) anchor[columnWidths+columnIndex] = 1; // Previous column widths
			anchor[columnWidths+index.x] = 1./2; // Centers anchored column
			anchor.constant = elements[elementIndex]->anchor.x * size.x -(index.x+1./2)*size.x/table.columnCount -margin.x;
		}
	}

	// • Preferred aspect ratio
	if(0) for(size_t elementIndex: freeAspects) {
		Constraint& preferAspectRatio = constraint();  // -ratio * height + width = 0
		setHeightCoefficient(preferAspectRatio, elementIndex, -elements[elementIndex]->aspectRatio);
		setWidthCoefficient(preferAspectRatio, elementIndex);
	}

	log("=", constraints.size, "constraints");

	// -- Solves regularized linear least square system
	Matrix A (constraints.size, unknownCount); Vector b (constraints.size);
	for(size_t i: range(constraints.size)) {
		const Constraint& constraint = constraints[i];
		for(size_t j: range(unknownCount)) A(i,j) = constraint[j];
		b[i] = constraint.constant;
	}
	Matrix At = transpose(A);
	Matrix AtA = At * A;
	Vector Atb = At * b;
	// Regularizes all margin/space offset unknowns
	for(size_t i: range(regularizedUnknownCount)) AtA(i,i) = AtA(i,i) + 1;
	// Solves AtA = Atb
	Vector x = solve(move(AtA),  Atb);
	Vector r = A*x - b; log(r);

	// -- Explicitly evaluates layout
	for(size_t elementIndex: range(elements.size)) {
		Element& element = elements[elementIndex];
		const size_t columnIndex = element.index.x;
		const size_t rowIndex = element.index.y;
		float height = x[elementHeights+elementIndex];
		size_t freeElementWidth = freeAspects.indexOf(elementIndex);
		float width = freeElementWidth == invalid ?
					element.aspectRatio * height // ratio · height
				  : x[elementsWidths+freeElementWidth]; // free width
		vec2 cellMin =
				margin +
				vec2(x[uniformMargin+0], x[uniformMargin+1]) +
				vec2(x[rowMargins+rowIndex], x[columnMargins+columnIndex]) +
				vec2(columnIndex*size.x/table.columnCount, rowIndex*size.y/table.rowCount) +
				vec2(sum(x.slice(columnWidths, columnIndex)), sum(x.slice(rowHeights, rowIndex)));
		vec2 cellSpanSize =
				/*space +
				vec2(x[uniformSpace+0], x[uniformSpace+1]) +
				vec2(x[columnSpaces+columnIndex], x[rowSpaces+rowIndex]) +*/
				vec2(element.cellCount.x*size.x/table.columnCount, element.cellCount.y*size.y/table.rowCount) +
				vec2(sum(x.slice(columnWidths+element.index.x, element.cellCount.x)),
						 sum(x.slice(rowHeights    +element.index.y, element.cellCount.y))) /*+
				vec2(x[columnSpaces+columnIndex], x[rowSpaces+rowIndex]) +
				vec2(x[uniformSpace+0], x[uniformSpace+1]) +
				space*/;
		element.min = cellMin + (cellSpanSize-vec2(width, height))/2.f;
		element.max = element.min + vec2(width, height);
	}

	if(1) {
		log("Constant margin", margin);
		log("Constant space", space);
		log("Uniform margin", x.slice(uniformMargin, 2));
		log("Uniform space", x.slice(uniformSpace, 2));
		log("Row margins", x.slice(rowMargins, table.rowCount));
		log("Column margins", x.slice(columnMargins, table.columnCount));
		log("Row spaces", x.slice(rowSpaces,  table.rowCount));
		log("Column spaces", x.slice(columnSpaces, table.columnCount));
		log("Column widths", x.slice(columnWidths, table.columnCount));
		log("Row heights", x.slice(rowHeights, table.rowCount));
		log("Element heights", x.slice(elementHeights, elements.size));
		if(freeAspects) log("Element widths", x.slice(elementsWidths, freeAspects.size));

		array<char> s;
		for(size_t rowIndex : range(table.rowCount)) {
			for(size_t columnIndex : range(table.columnCount)) {
				const Cell& cell = table(columnIndex, rowIndex);
				if(cell.horizontalExtension) s.append("-");
				if(cell.verticalExtension) s.append("|");
				const Element& e = elements[cell.parentElementIndex];
				//s.append(str(strx(int2(round(e.min))), strx(int2(round(e.max-e.min))), "\t"));
				s.append(str(strx(int2(round(e.min))), strx(int2(round(e.max))), "\t"));
			}
		}
		log(s);
	}
}
