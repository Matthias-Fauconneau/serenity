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

struct FileTextEdit : ScrollTextEdit {
	String fileName;
	FileTextEdit(string fileName, array<Scope>& scopes, function<void(array<Scope>&, string)> parse) : ScrollTextEdit(move(Parser(fileName, scopes, parse).target)), fileName(copyRef(fileName)) {}
};

struct IDE {
	array<Scope> scopes;
	map<String, FileTextEdit> edits;
	FileTextEdit* current = 0;
	unique<Window> window = ::window(null, 1024);
	struct Location { String fileName; Cursor cursor; };
	array<Location> viewHistory;

	IDE() {
		scopes.append(); // Global scope
		view("test.cc");
	}

	void parse(array<Scope>& scopes, string fileName) {
		FileTextEdit edit(fileName, scopes, {this, &IDE::parse});
		edit.edit.linkActivated = [this](ref<uint> identifier) {
			assert_(identifier);
			String fileName = toUTF8(identifier.slice(0, identifier.size-1));
			size_t index = identifier.last();
			view(fileName, index);
		};
		edit.edit.back = [this] {
			Location location = viewHistory.pop();
			current = &edits.at(location.fileName);
			current->edit.cursor = location.cursor;
			current->ensureCursorVisible();
			if(window) window->widget = current;
		};
		edits.insert(copyRef(fileName), move(edit));
	}

	void view(string fileName, uint index=0) {
		if(!edits.contains(fileName)) parse(scopes, fileName);
		viewHistory.append(Location{copyRef(current->fileName), current->edit.cursor});
		current = &edits.at(fileName);
		current->edit.cursor = current->edit.cursorFromIndex(index);
		current->ensureCursorVisible();
		if(window) window->widget = current;
	}
} app;
