#include "parser.h"
#include "edit.h"
#include "interface.h"
#include "window.h"

struct ScrollTextEdit : ScrollArea {
	TextEdit edit;

	ScrollTextEdit(buffer<uint>&& text) : edit(move(text)) {}

	Widget& widget() override { return edit; }

	void ensureCursorVisible() {
		if(!viewSize) return;
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

generic String diff(ref<T> a, ref<T> b) {
	array<char> s;
	uint lineIndex = 0;
	for(size_t i: range(min(a.size, b.size))) {
		if(a[i] == '\n') lineIndex++;
		if(a[i] != b[i]) {
			s.append(str(lineIndex)/*+'\t'+str(a[i])+'\t'+str(a[i], 16u, '0', 2u)+'\t'+str(b[i])+'\t'+str(b[i], 16u, '0', 2u)+'\n'*/);
			break;
		}
	}
	return move(s);
}

struct FileTextEdit : ScrollTextEdit {
	String fileName;
	FileTextEdit(string fileName, Scope& module, function<void(Scope&, string)> parse) :
		ScrollTextEdit(move(Parser(readFile(find(fileName)), fileName, module, parse).target)), fileName(copyRef(fileName)) {
		if(toUTF8(strip(edit.text)) != readFile(find(fileName))) error(diff(toUTF8(strip(edit.text)), readFile(find(fileName))), fileName);
	}
	String title() override { return copyRef(fileName); }
};

struct Editor {
	Scope module;
	map<String, FileTextEdit*> edits;
	FileTextEdit* current = 0;
	unique<Window> window = ::window(null, 1024);
	struct Location { String fileName; Cursor cursor; };
	array<Location> viewHistory;

	Editor() {
		view("test.cc");
	}

	void parse(Scope& module, string fileName) {
		if(!edits.contains(fileName)) {
			FileTextEdit* edit = new FileTextEdit(fileName, module, {this, &Editor::parse});
			edit->edit.linkActivated = [this](ref<uint> identifier) {
				assert_(identifier);
				String fileName = find(toUTF8(identifier.slice(0, identifier.size-1)));
				size_t index = identifier.last();
				view(fileName, index);
			};
			edit->edit.back = [this] {
				if(viewHistory) {
					Location location = viewHistory.pop();
					current = edits.at(location.fileName);
					current->edit.selectionStart = current->edit.cursor = location.cursor;
					current->ensureCursorVisible();
					if(window) window->widget = current;
				}
			};
			edit->edit.textChanged = [this, edit](ref<uint>) {
				this->module.scopes.filter([&](string, const Scope& o) { return o.fileName == edit->fileName; });
				this->module.types.filter([&](string, const ::Location& o) { return startsWith(o, toUCS4(edit->fileName)); });
				this->module.variables.filter([&](string, const ::Location& o) { return startsWith(o, toUCS4(edit->fileName)); });
				String source = toUTF8(strip(edit->edit.text));
				edit->edit.text = move(Parser(source, edit->fileName, this->module, {this, &Editor::parse}).target);
				assert_(existsFile(edit->fileName));
				writeFile(edit->fileName, source, currentWorkingDirectory(), true);
				// FIXME: invalidate users
			};
			edits.insert(copyRef(fileName), move(edit));
		}
	}

	void view(string fileName, uint index=0) {
		if(!edits.contains(fileName)) parse(module, fileName);
		if(current) viewHistory.append(Location{copyRef(current->fileName), current->edit.cursor});
		current = edits.at(fileName);
		current->edit.selectionStart = current->edit.cursor = current->edit.cursorFromIndex(index);
		current->ensureCursorVisible();
		if(window) { window->widget = window->focus = current; window->render(); }
	}
} app;
