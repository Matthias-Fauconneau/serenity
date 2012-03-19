#include "launcher.h"
#include "map.h"
#include "stream.h"
#include "process.h"
#include "window.h"
#include "interface.h"
#include "file.h"

#include "array.cc"
template class array<Command>;

const string iconPaths[4] = {
    "/usr/share/pixmaps/"_,
    "/usr/share/icons/oxygen/$size/apps/"_,
    "/usr/share/icons/hicolor/$size/apps/"_,
    "/usr/local/share/icons/hicolor/$size/apps/"_
};

bool Search::keyPress(Key key) {
    if(key == Return) {
        execute("/usr/lib64/chromium-browser/chromium-launcher.sh"_,{"google.com/search?q="_+text});
        text.clear(); update(); triggered.emit(); return true;
    }
    else return TextInput::keyPress(key);
}

bool Command::mouseEvent(int2, Event event, Button button) {
    if(event == Press && button == LeftButton) { execute(exec); triggered.emit(); return true; }
    return false;
}

bool Menu::mouseEvent(int2 position, Event event, Button button) {
    if(Vertical::mouseEvent(position,event,button)) return true;
    if(event==Leave) close.emit();
    return false;
}

bool Menu::keyPress(Key key) {
    if(Vertical::keyPress(key)) return true;
    if(key==Escape) { close.emit(); return true; }
    return false;
}

map<string,string> readConfig(const string& path) {
    map<string,string> entries;
    for(TextBuffer s(readFile(path));s;) {
        if(s.match("["_)) s.until("\n"_);
        else {
            string key = s.until("="_), value=s.until("\n"_);
            entries.insert(move(key),move(value));
        }
        s.whileAny("\n"_);
    }
    return entries;
}

List<Command> readShortcuts() {
    List<Command> shortcuts;
    auto config = readConfig(strz(getenv("HOME"))+"/.config/launcher"_);
    for(const string& desktop: split(config["Favorites"_],',')) {
        if(!exists(desktop)) continue;
        auto entries = readConfig(desktop);
        Image icon;
        for(const string& folder: iconPaths) {
            string path = replace(folder,"$size"_,"32x32"_)+entries["Icon"_]+".png"_;
            if(exists(path)) { icon=resize(Image(readFile(path)), 32,32); break; }
        }
        auto execPaths = {"/usr/bin/"_,"/usr/local/bin/"_};
        string exec;
        for(const string& folder: execPaths) {
            string path = folder+section(entries["Exec"_],' ');
            if(exists(path)) { exec=move(path); break; }
        }
        assert(exec,exec);
        shortcuts << Command(move(icon),move(entries["Name"_]),move(exec));
    }
    return shortcuts;
}

Launcher::Launcher() : shortcuts(readShortcuts()), menu(i({ &search, &shortcuts })), window(&menu,int2(-3,-3)) {
    menu.close.connect(&window,&Window::hide);
    search.triggered.connect(&window,&Window::hide);
    for(auto& shortcut: shortcuts) shortcut.triggered.connect(&window,&Window::hide);
    window.setType("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"_);
    window.setOverrideRedirect(true);
}

void Launcher::show() { window.show(); window.setPosition(int2(0,0)); window.setFocus(&search); }
void Launcher::keyPress(Key key) { if(key==Escape) window.hide(); }
