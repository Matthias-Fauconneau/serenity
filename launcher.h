#pragma once
#include "window.h"
#include "interface.h"

constexpr ref<byte> iconPaths[4] = {
    "/usr/share/pixmaps/"_,
    "/usr/share/icons/oxygen/$size/apps/"_,
    "/usr/share/icons/hicolor/$size/apps/"_,
    "/usr/local/share/icons/hicolor/$size/apps/"_
};

struct Search : TextInput {
    string browser;
    signal<> triggered;
    bool keyPress(Key key) override;
};

struct Command : Item {
    signal<> triggered;
    string path; array<string> args;
    Command(Image&& icon, string&& text, string&& path, array<string>&& args) :
        Linear(Left),Item(move(icon),move(text)),path(move(path)),args(move(args)){}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button) override;
};

List<Command> readShortcuts();

struct Launcher {
    Search search;
    List<Command> shortcuts = readShortcuts();
    VBox menu;// = __(&search, &shortcuts);
    Window window __(&menu,int2(-128,-128));
    Launcher();
};
