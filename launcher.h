#pragma once
#include "window.h"
#include "interface.h"

extern const string iconPaths[4];

struct Search : TextInput {
    string browser;
    signal<> triggered;
    bool keyPress(Key key) override;
};

struct Command : Item {
    signal<> triggered;
    string path; array<string> args;
    Command(Icon&& icon, Text&& text, string&& path, array<string>&& args):Item(move(icon),move(text)),path(move(path)),args(move(args)){}
    bool mouseEvent(int2, Event event, Button) override;
};

/// Menu is a \a VBox which send \a close signal on mouse leave or when accepting any event.
/// \note Dropdown menus can be implemented by embedding \a Menu in a \a Window
struct Menu : VBox {
    signal<> close;
    Menu(std::initializer_list<Widget*>&& widgets):VBox(move(widgets)){}
    bool mouseEvent(int2 position, Event event, Button button) override;
    bool keyPress(Key key) override;
};

List<Command> readShortcuts();

struct Launcher {
    Search search;
    List<Command> shortcuts;
    Menu menu;
    Window window;

    Launcher();
    void show();
    void keyPress(Key key);
};
