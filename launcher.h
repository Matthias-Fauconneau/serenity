#pragma once
#include "window.h"
#include "interface.h"

struct Search : TextInput {
    bool keyPress(Key key) override;
};

struct Shortcut : Item {
    string exec;
    Shortcut(Icon&& icon, Text&& text, string&& exec):Item(move(icon),move(text)),exec(move(exec)){}
    bool mouseEvent(int2, Event event, Button) override;
};

/// Menu is a \a VBox which send \a close signal on mouse leave or when accepting any event.
/// \note Dropdown menus can be implemented by embedding \a Menu in a \a Window
struct Menu : VBox {
    bool mouseEvent(int2 position, Event event, Button button) override;
};

struct Launcher {
    Menu menu;
    Window window = Window(menu,int2(128,16+256),"Menu"_);
    Search search;
    List<Shortcut> shortcuts;

    Launcher();
    void show();
    void keyPress(Key key);
};
