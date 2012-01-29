//TODO: Menu: /Applications/Categories/application, /Files/(Favorites|Devices|Recent Files|URLs)/file, rounded window
//TODO: Search: {Applications, Files, Web, Command}, completion in popup (+recent queries)
//TODO: Desktop: Applications, Favorites/Places/Devices/Recent Files/URLs, Folder, Feeds/Messages/Contacts, Events, Search, PowerOff
#include "launcher.h"
#include "process.h"
#include "window.h"
#include "interface.h"
#include "file.h"
#include "stdlib.h"

static signal<> closeMenu;
static string browser;

bool Search::keyPress(Key key) {
    if(key == Return) {
        execute(browser,{"google.com/search?q="_+move(text)});
        text.clear(); update(); closeMenu.emit();
        return true;
    }
    else return TextInput::keyPress(key);
}

bool Shortcut::mouseEvent(int2, Event event, Button) {
    if(event == Press) { execute(exec); closeMenu.emit(); return true; }
    return false;
}

bool Menu::mouseEvent(int2 position, Event event, Button button) {
    if(Vertical::mouseEvent(position,event,button)) return true;
    if(event==Leave) {
        closeMenu.emit();
    }
    return false;
}

map<string,string> readConfig(const string& file) {
    map<string,string> entries;
    for(const string& line: split(mapFile(file),'\n')) {
        if(line.contains('=')) entries[section(line,'=')]=section(line,'=',1,-1);
    }
    return entries;
}

Launcher::Launcher() {
    map<string,string> config = readConfig(strz(getenv("HOME"))+"/.config/launcher"_);
    browser = move(config["Browser"_]);
    assert(exists(browser));
    for(const string& desktop: split(config["Favorites"_],',')) {
        map<string,string> entries = readConfig(desktop);
        auto iconPaths = {"/usr/share/icons/oxygen/32x32/apps/"_,
                          "/usr/share/icons/hicolor/32x32/apps/"_,
                          "/usr/local/share/icons/hicolor/32x32/apps/"_,
                          "/usr/share/pixmaps/"_ };
        Image icon;
        for(const string& folder: iconPaths) {
            string path = folder+entries["Icon"_]+".png"_;
            if(exists(path)) { icon=move(Image(mapFile(path)).resize(32,32)); break; }
        }
        auto execPaths = {"/usr/bin/"_,"/usr/local/bin/"_};
        string exec;
        for(const string& folder: execPaths) {
            string path = folder+section(entries["Exec"_],' ');
            if(exists(path)) { exec=move(path); break; }
        }
        assert(exec,exec);
        shortcuts << Shortcut(move(icon),move(entries["Name"_]),move(exec));
    }
    //TODO: parse /usr/share/applications/*.desktop to categories (Network Graphics AudioVideo Office Utility System)
    menu << search << shortcuts.parent();
    shortcuts.expanding = true;
    menu.update();

    window.keyPress.connect(this,&Launcher::keyPress);
    closeMenu.connect(&window,&Window::hide);
    window.setType("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"_);
    window.setOverrideRedirect(true);
}

void Launcher::show() { window.show(); window.move(int2(0,0)); window.sync(); window.setFocus(&search); window.sync(); }
void Launcher::keyPress(Key key) { if(key==Escape) window.hide(); }
