#include "parser.h"
#include "edit.h"
#include "interface.h"
#include "window.h"

struct ScrollTextEdit : ScrollArea {
	TextEdit edit;

	ScrollTextEdit(buffer<uint>&& text) : edit(move(text)) {}

	Widget& widget() override { return edit; }

	void ensureCursorVisible() {
		vec2 size = viewSize;
		vec2 position = edit.cursorPosition(size, edit.cursor);
		if(position.y < -offset.y) offset.y = -(position.y);
		if(position.y+edit.Text::size > -offset.y+size.y) offset.y = -(position.y-size.y+edit.Text::size);
	}

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) override {
		bool contentChanged = ScrollArea::mouseEvent(cursor, size, event, button, focus) || edit.mouseEvent(cursor, size, event, button, focus);
		focus = this;
		return contentChanged;
	}
	bool keyPress(Key key, Modifiers modifiers) override {
		if(edit.keyPress(key, modifiers)) {
			ensureCursorVisible();
			return true;
		}
		else return ScrollArea::keyPress(key, modifiers);
	}
};

struct IDE {
	map<String, ScrollTextEdit> edits;
	unique<Window> window = ::window(null, 1024);
	IDE() { view("test.cc"); }
	void view(string fileName, uint index=0) {
		if(!edits.contains(fileName)) {
			edits.insert(copyRef(fileName), move(Parser(fileName).target));
			edits.at(fileName).edit.linkActivated = [this](ref<uint> identifier) {
				assert_(identifier);
				String fileName = toUTF8(identifier.slice(0, identifier.size-1));
				size_t index = identifier.last();
				view(fileName, index);
			};
		}
		ScrollTextEdit& edit = edits.at(fileName);
		edit.edit.cursor = edit.edit.cursorFromIndex(index);
		edit.ensureCursorVisible();
		if(window) window->widget = &edit;
	}
} app;
