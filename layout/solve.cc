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
	size_t elementsWidths = unknownIndex; unknownIndex += freeAspects.size; log(freeAspects.size, "element widths");

	const size_t unknownCount = unknownIndex;
	log("=", unknownCount, "unknowns");

	// -- Constraints
	struct Constraint : buffer<float>{
		float constant = 0;
		Constraint(size_t size) : buffer(size) { clear(0); }
		float& operator[](int unknownIndex) { assert(at(unknownIndex)==0); return at(unknownIndex); }
		float operator[](int unknownIndex) const { return at(unknownIndex); }
	};
	// Sets height coefficient
	auto setHeightCoefficient = [=](Constraint& constraint, size_t elementIndex, float coefficient=1) {
		constraint[elementHeights+elementIndex] = coefficient;
	};
	// Sets width coefficient handling either fixed or free aspect ratio transparently
	auto setWidthCoefficient = [=](Constraint& constraint, size_t elementIndex, float coefficient=1) {
		assert(constraint[elementHeights+elementIndex]==0);
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
		for(const size_t columnIndex : range(table.rowCount)) fitColumnWidths[columnWidths+columnIndex] = 1;
		fitColumnWidths[uniformMargin+0] = 2; // Uniform margin x
		fitColumnWidths[rowMargins+rowIndex] = 2; // Row margin x
		fitColumnWidths.constant = size.x - 2*margin.x;
	}

	// • Fits row heights
	for(size_t columnIndex: range(table.columnCount)) {
		Constraint& fitRowHeights = constraint(); // sum(heights) + 2·margin = height
		for(const size_t rowIndex : range(table.rowCount)) fitRowHeights[rowHeights+rowIndex] = 1;
		fitRowHeights[uniformMargin+1] = 2; // Uniform margin y
		fitRowHeights[columnMargins+columnIndex] = 2; // Column margin y
		fitRowHeights.constant = size.x - 2*margin.y;
	}

	// • Fits elements to their columns/rows
	for(size_t elementIndex : range(elements.size)) {
		const Element& element = elements[elementIndex];
		//log(element.index, element.cellCount);
		{  Constraint& fitElementWidth = constraint();  // width + 2·column space = columns width
			setWidthCoefficient(fitElementWidth, elementIndex);
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
			fitElementWidth.constant = -2*space.x;
		}
		{  Constraint& fitElementHeight = constraint();  // height + 2·space = row height
			setHeightCoefficient(fitElementHeight, elementIndex);
			if(element.cellCount.y == 1) {
				fitElementHeight[columnSpaces+element.index.y] = 2; // Row space
				fitElementHeight[rowHeights+element.index.y] = -1; // Row height
			} else {
				fitElementHeight[rowSpaces+element.index.y] = 1; // Row space
				for(size_t rowIndex: range(element.index.y, element.index.y+element.cellCount.y))
					fitElementHeight[rowHeights+rowIndex] = -1; // Row height
				fitElementHeight[rowSpaces+element.index.y+element.cellCount.y-1] = 1; // Row space
			}
			fitElementHeight.constant = -2*space.y;
		}
	}

	// • Horizontal anchors
	for(size_t anchorElementIndex: horizontalAnchors) {
		for(const size_t rowIndex : range(table.rowCount)) {
			if(!table.row(rowIndex).contains(anchorElementIndex)) continue;
			Constraint& anchor = constraint();
			for(const size_t columnIndex: range(table.columnCount)) {
				Cell& cell = table(columnIndex, rowIndex);
				size_t elementIndex = cell.parentElementIndex;
				if(elementIndex==anchorElementIndex) {
					setWidthCoefficient(anchor, elementIndex, 1./2); // Anchor center
					break;
				} else {
					setWidthCoefficient(anchor, elementIndex);
				}
			}
			anchor[uniformMargin+0] = 1; // Uniform margin x
			anchor[rowMargins+rowIndex] = 1; // Row margin x
			anchor.constant = elements[anchorElementIndex]->anchor.x * size.x - margin.x;
		}
	}

#if 0
	// • Preferred aspect ratio
	for(size_t elementIndex: range(elements.size)) {
		if(aspectRatios[elementIndex] < 0) {
			float aspectRatioWeight = 1; //1./k;
			A(equationIndex, elementIndex) = aspectRatioWeight* (-abs(aspectRatios[elementIndex])); // -r*h + w = 0
			assert_(A(equationIndex, elementIndexToUnknownIndex[elementIndex]) == 0);
			A(equationIndex, elementIndexToUnknownIndex[elementIndex]) = aspectRatioWeight* 1;
			b[equationIndex] = aspectRatioWeight* 0;
			log("Preferable aspect ratio hint", elementIndex);
			equationIndex++;
		}
	}
#endif

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
		element.min =
				vec2(x[uniformMargin+0], x[uniformMargin+1]) +
				vec2(x[columnMargins+columnIndex], x[rowMargins+rowIndex]) +
				vec2(sum(x.slice(columnWidths, columnIndex)), sum(x.slice(rowHeights, rowIndex)))
				- vec2(width, height)/2.f;
		element.max = element.min + vec2(x[columnWidths+columnIndex], x[rowHeights+rowIndex]) + vec2(width, height)/2.f;
	}

	if(1) {
		log("Uniform margin", x.slice(uniformMargin, 2));
		log("Uniform space", x.slice(uniformSpace, 2));
		log("Row margins", x.slice(rowMargins, table.rowCount));
		log("Column margins", x.slice(columnMargins, table.columnCount));
		log("Row spaces", x.slice(rowSpaces,  table.rowCount));
		log("Column spaces", x.slice(columnSpaces, table.columnCount));
		log("Column widths", x.slice(columnWidths, table.columnCount));
		log("Row heights", x.slice(rowHeights, table.rowCount));
		log("Element heights", x.slice(elementHeights, elements.size));
		log("Element widths", x.slice(elementsWidths, freeAspects.size));

		array<char> s;
		for(size_t rowIndex : range(table.rowCount)) {
			for(size_t columnIndex : range(table.columnCount)) {
				const Cell& cell = table(columnIndex, rowIndex);
				if(cell.horizontalExtension) s.append("-");
				if(cell.verticalExtension) s.append("|");
				const Element& e = elements[cell.parentElementIndex];
				s.append(str(strx(int2(round(e.min))), strx(int2(round(e.max-e.min))), "\t"));
			}
		}
		log(s);
	}
}
