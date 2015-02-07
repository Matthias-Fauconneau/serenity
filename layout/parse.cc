#include "parse.h"
#include "file.h"
#include "jpeg.h"
#include "png.h"
#include "text.h"
#include "ui/render.h"
#include "thread.h"

struct ImageElement : Element {
	Map file;
	ImageElement(string fileName, const Folder& folder) : file(fileName , folder) {
		vec2 size = vec2(::imageSize(file));
		aspectRatio = (float)size.x/size.y;
		sizeHint = size/300.f*inchMM;
	}
	Image image(float) const override {
		Image image = decodeImage(file);
		return image;
	}
};
struct TextElement : Element {
	String string;
	float textSize = 12;
	bool transpose = false;
	bool center = true;
	TextElement(::string text, float textSize, bool transpose, bool center)
		: string(copyRef(text)), textSize(textSize), transpose(transpose), center(center) {
		vec2 size = Text(text, textSize/72*72, white, 1, 0, "LinLibertine", false, 1, center).sizeHint();
		if(transpose) swap(size.x, size.y);
		aspectRatio = (float)size.x/size.y;
	}
	Image image(float mmPx) const override {
		int2 size = this->size(mmPx);
		if(transpose) swap(size.x, size.y);
		Text text(string, textSize/72*inchMM*mmPx, white, 1, (floor(max.x*mmPx) - ceil(min.x*mmPx) - 4)*2/3 /*FIXME*/, "LinLibertine", false, 1, center);
		Image image = render(size, text.graphics(vec2(size)));
		if(transpose) image = rotateHalfTurn(rotate(image));
		return image;
	}
};

#undef assert
#define assert(expr, message...) ({ if(!(expr)) { error(#expr ""_, ## message); return; } })

LayoutParse::LayoutParse(const Folder& folder, TextData&& s, function<void(string)> logChanged, FileWatcher* watcher)
	: logChanged(logChanged) {
	const ref<string> parameters = {"size","margin","space","chroma","intensity","hue","blur","feather"};
	// -- Parses arguments
	for(;;) {
		int nextLine = 0;
		while(s && s[nextLine] != '\n') nextLine++;
		string line = s.peek(nextLine);
		if(startsWith(line, "#")) { s.until('\n'); continue; }
		if(line.contains('=')) {
			string key = s.whileNo(" \t\n=");
			assert(parameters.contains(key), "Unknown parameter", key, ", expected", parameters);
			s.whileAny(" \t");
			s.skip("=");
			s.whileAny(" \t");
			string value = s.whileNo(" \t\n");
			s.whileAny(" \t");
			s.skip("\n");
			arguments.insert(copyRef(key), copyRef(value));
		} else break;
	}
	size = argument<vec2>("size"), margin = value<vec2>("margin"_, 20), space = value<vec2>("space"_, 15);
	s.whileAny(" \n");

	// -- Parses table
	array<String> files = folder.list(Files);
	files.filter([](string name){return !(endsWith(name,".png") || endsWith(name, ".jpg") || endsWith(name, ".JPG"));});
	array<array<int>> rows;

	while(s) {
		array<int> row;
		while(!s.match("\n")) {
			if(s.match("#")) { s.until('\n'); continue; }
			/***/ if(s.match("-")) row.append(-1);
			else if(s.match("|")) { columnStructure=true; row.append(-2); }
			else if(s.match("\\")) row.append(-3);
			else {
				unique<Element> element = nullptr;
				/**/ if(s.match("@")) { // Indirect text
					string name = s.whileNo(" \t\n");
					if(watcher) watcher->addWatch(name);
					element = unique<TextElement>(readFile(name), 12.f, false, false);
					freeAspects.append(elements.size);
				} else if(s.match("\"")) { // Text
					String text = replace(s.until('"'),"\\n","\n");
					string textSize = s.whileDecimal();
					bool transpose = s.match("T");
					element = unique<TextElement>(text, textSize ? parseDecimal(textSize) : 12, transpose, true);
					//freeAspects.append(elements.size);
				} else { // Image
					string name = s.whileNo("*! \t\n");
					string file = [&](string name) { for(string file: files) if(startsWith(file, name+".")) return file; return ""_; }(name);
					if(!file) { error("No such image"_, name, "in", files); return; }
					if(watcher) watcher->addWatch(file);
					element = unique<ImageElement>(file, folder);
				}
				if(s.match("!")) element->anchor.x = 1./2;
				if(s.match("*")) preferredSize.append(elements.size);
				if(element->anchor.x) horizontalAnchors.append(elements.size);
				if(element->anchor.y) verticalAnchors.append(elements.size);
				element->index = int2(row.size, rows.size);
				row.append(elements.size); // Appends element index to row
				elements.append(move(element));
			}
			s.whileAny(" \t"_);
		}
		assert(row);
		// Automatically generate table structure from row-wise definition
		for(auto& o : rows) {
			if(o.size < row.size) { assert(o.size==1); o.append(-1); rowStructure=true; gridStructure=false; }
			if(row.size < o.size) { row.append(-1); rowStructure=true; gridStructure=false; }
		}
		rows.append(move(row));
		if(!s) break;
	}
	if(rows.size==1) rowStructure=true;
	assert(rows);
	size_t columnCount = rows[0].size;
	table = int2(columnCount, rows.size);
	for(const size_t y: range(table.rowCount)) {
		assert(rows[y].size == columnCount);
		for(const size_t x: range(table.columnCount)) {
			int2 index (x,y);
			if(rows[y][x] >= 0) {
				table(index) = {size_t(rows[y][x]), index, 1, false, false};
			} else {
				Cell& cell = table(x, y);
				if(rows[y][x] == -1) cell.horizontalExtension = true;
				if(rows[y][x] == -2) cell.verticalExtension = true;
				if(rows[y][x] == -3) cell.horizontalExtension = true, cell.verticalExtension = true;
				int2 size = 1;
				while(rows[index.y][index.x] == -3) index.x--, index.y--, size.x++, size.y++;
				while(rows[index.y][index.x] == -2) index.y--, size.y++;
				while(rows[index.y][index.x] == -1) index.x--, size.x++;
				Cell& parent = table(index);
				parent.parentSize = max(parent.parentSize, size);
				cell.parentIndex = parent.parentIndex;
			}
		}
	}
	for(Cell& cell : table.cells) {
		cell.parentElementIndex = table(cell.parentIndex).parentElementIndex;
		cell.parentSize = table(cell.parentIndex).parentSize;
		assert_(elements[cell.parentElementIndex]->index == cell.parentIndex);
		elements[cell.parentElementIndex]->cellCount = cell.parentSize;
	}
	if(table.columnCount == 1) columnStructure=true;
	if(table.columnCount == rows.size) gridStructure = true;
	log(strx(int2(size)), strx(int2(margin)), strx(int2(space)), strx(table.size),
		gridStructure?"grid":"", rowStructure?"row":"", columnStructure?"column":"");
}
