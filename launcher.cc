#include "launcher.h"
#include "map.h"
#include "stream.h"
#include "process.h"
#include "window.h"
#include "interface.h"
#include "file.h"

#include "array.cc"
template class array<Command>;

bool Search::keyPress(Key key) {
    if(key == Return) {
        execute("/usr/lib64/chromium-browser/chromium-launcher.sh"_,{"google.com/search?q="_+text});
        text.clear(); update(); return true;
    }
    else return TextInput::keyPress(key);
}

bool Command::mouseEvent(int2, Event event, Button) {
    if(event == Press) { execute(exec); return true; }
    return false;
}

bool Menu::mouseEvent(int2 position, Event event, Button button) {
    if(Vertical::mouseEvent(position,event,button)) { close.emit(); return true; }
    if(event==Leave) close.emit();
    return false;
}

bool Menu::keyPress(Key key) {
    if(Vertical::keyPress(key)) {
        if(key==Return) { close.emit(); return true; }
    } else if(key==Escape) { close.emit(); return true; }
    return false;
}

map<string,string> readConfig(const string& path) {
    map<string,string> entries;
    for(Stream s(mapFile(path));s;) {
        if(s.match("["_)) s.until('\n');
        else {
            string key = s.until('='), value=s.until('\n');
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
        auto iconPaths = {"/usr/share/pixmaps/"_,
                          "/usr/share/icons/oxygen/32x32/apps/"_,
                          "/usr/share/icons/hicolor/32x32/apps/"_,
                          "/usr/local/share/icons/hicolor/32x32/apps/"_};
        Image icon;
        for(const string& folder: iconPaths) {
            string path = folder+entries["Icon"_]+".png"_;
            if(exists(path)) { icon=resize(Image(mapFile(path)), 32,32); break; }
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

Launcher::Launcher() : shortcuts(readShortcuts()), menu({ &search, &shortcuts }), window(&menu,int2(-3,-3)) {
    window.keyPress.connect(this,&Launcher::keyPress);
    menu.close.connect(&window,&Window::hide);
    window.setType("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"_);
    window.setOverrideRedirect(true);
}

void Launcher::show() { window.show(); window.setPosition(int2(0,0)); window.setFocus(&search); }
void Launcher::keyPress(Key key) { if(key==Escape) window.hide(); }
