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
