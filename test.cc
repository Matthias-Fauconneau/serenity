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

	bool mouseEvent(vec2 cursor, vec2 size, Event event, Button button, Widget*& focus) {
		bool contentChanged = ScrollArea::mouseEvent(cursor, size, event, button, focus) || edit.mouseEvent(cursor, size, event, button, focus);
		focus = this;
		return contentChanged;
	}
	bool keyPress(Key key, Modifiers modifiers) {
		if(edit.keyPress(key, modifiers)) {
			ensureCursorVisible();
			return true;
		}
		else return ScrollArea::keyPress(key, modifiers);
	}
};

struct ModuleEdit : ScrollTextEdit {
	String fileName;
	ModuleEdit(string fileName) : ScrollTextEdit(move(Parser(fileName).target)), fileName(copyRef(fileName)) {
		edit.linkActivated = [this](ref<uint> identifier) {
			String fileName = toUTF8(identifier.slice(0, identifier.size-1));
			size_t index = identifier.last();
			assert_(fileName == this->fileName, "TODO: multiple file navigation");
			edit.cursor = edit.cursorFromIndex(index);
			ensureCursorVisible();
		};
	}
};

struct ModuleEditApplication {
	ModuleEdit moduleEdit {"test.cc"};
	unique<Window> window = ::window(&moduleEdit, 1024);
} app;
