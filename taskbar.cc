#include "process.h"
#include "file.h"
#include "time.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "calendar.h"
#include "popup.h"
#include "x.h"
#if DBUS
#include "dbus.h"
#endif

#ifndef __GXX_EXPERIMENTAL_CXX0X__
#include "X11/Xlib.h"
#endif

#include "array.cc"

template struct Popup<Calendar>;
template struct Array<Command>;
template struct ListSelection<Command>;
template struct List<Command>;

static Display* x;

string getTitle(XID id) {
    string name = Window::getProperty<byte>(id,"_NET_WM_NAME");
    if(!name) name = Window::getProperty<byte>(id,"WM_NAME");
    return move(name);
}
Image getIcon(XID id) {
    array<XID> buffer = Window::getProperty<XID>(id,"_NET_WM_ICON");
    if(buffer.size()<=4) return Image();
    array<byte4> image(buffer.size()); image.setSize(buffer.size());
    for(uint i=0;i<buffer.size()-2;i++) image[i] = *(byte4*)&buffer[i+2];
    return resize(Image(move(image), buffer[0], buffer[1]), 16,16);
}

static XID* topLevelWindowList=0;
static array<XID> getWindows() {
    if(topLevelWindowList) XFree(topLevelWindowList);
    XID root,parent; uint count=0; XQueryTree(x,x->screens[0].root,&root,&parent,&topLevelWindowList,&count);
    return array<XID>(topLevelWindowList,count);
}

void raise(XID id) {
    XRaiseWindow(x, id);
    XSetInputFocus(x, id, 1, 0);
    for(XID w: getWindows()) {
        if(Window::getProperty<Atom>(w,"WM_TRANSIENT_FOR") == array<Atom>{id}) {
            XRaiseWindow(x, w);
            XSetInputFocus(x, w, 1, 0);
        }
    }
}
struct Task : Item {
    XID id;
    Task(XID id):id(id){} //for indexOf
    Task(XID id, Icon&& icon, Text&& text):Item(move(icon),move(text)),id(id){}
    bool selectEvent() override { //Raise
        raise(id);
        XFlush(x);
        return true;
    }
    bool mouseEvent(int2, Event event, Button button) override {
        //TODO: preview on hover
        if(button!=LeftButton) return false;
        if(event==Press) {
            if(getWindows().last()==id) { //Set maximized
                XMoveResizeWindow(x,id, 0, 16, Window::screen.x,Window::screen.y-16);
                XFlush(x);
                return true;
            }
        }
        /*if(event==Motion) { //Set windowed
            XMoveResizeWindow(x,id, Window::screen.x/4,16+(Window::screen.y-16)/4,Window::screen.x/2,(Window::screen.y-16)/2);
            XFlush(x);
            return true;
        }*/
        return false;
    }
};
bool operator==(const Task& a,const Task& b){return a.id==b.id;}
Array_Compare(Task)
template struct Array<Task>;
template struct ListSelection<Task>;
template struct Bar<Task>;

#if DBUS
struct StatusNotifierItem : TriggerButton {
    DBus::Object item;
    string id;
    StatusNotifierItem(DBus::Object&& item, Image&& icon, string&& id) : TriggerButton(move(icon)), item(move(item)), id(move(id)) {}
    bool mouseEvent(int2 position, Event event, Button button) override {
        if(TriggerButton::mouseEvent(position,event,button)) {
            int2 c=Window::cursor;
            if(button==LeftButton) {
                for(XID w: getWindows()) { //Assume Activate toggle a window (doing it in taskbar as applications don't properly detect if they are visible)
                    XWindowAttributes wa; XGetWindowAttributes(x, w, &wa); if(wa.override_redirect||wa.map_state!=IsViewable) continue;
                    if(contains(getTitle(w),id)) { XUnmapWindow(x, w); XFlush(x); return true; }
                }
                item.noreply("org.kde.StatusNotifierItem.Activate"_, 0,0);
            }
            if(button==RightButton) item.noreply("org.kde.StatusNotifierItem.ContextMenu"_, c.x, 16);
            if(button==MiddleButton) item.noreply("org.kde.StatusNotifierItem.SecondaryActivate"_, 0,0);
            if(button==WheelDown) item.noreply("org.kde.StatusNotifierItem.Scroll"_, -1, "vertical"_);
            if(button==WheelUp) item.noreply("org.kde.StatusNotifierItem.Scroll"_, 1, "vertical"_);
            return true;
        }
        return false;
    }
};
Array(StatusNotifierItem)
template struct Array<StatusNotifierItem>;
template struct ListSelection<StatusNotifierItem>;
template struct Bar<StatusNotifierItem>;
template array<byte4> cast(array<byte>&& array);
#endif

ICON(button);
struct TaskBar : Application, Poll {
    signal<int> tasksChanged;

      TriggerButton start;
       Launcher launcher;
      Bar<Task> tasks;
      Clock clock;
       Popup<Calendar> calendar;
#if DBUS
      DBus dbus
      Bar<StatusNotifierItem> status;
      HBox panel {&start, &tasks, &status, &clock };
#else
       HBox panel {&start, &tasks, &clock };
#endif
      Window window{&panel,""_,Image(),int2(0,-1)};

    void startButton() {
        if(!launcher.window.visible) { launcher.show(); return; }
        launcher.window.hide();
        tasks.setActive(-1);
        window.render();
        for(XID id: getWindows()) {
            array<Atom> type=Window::getProperty<Atom>(id,"_NET_WM_WINDOW_TYPE");
            if(type.size()>=1 && (type[0]==Atom(_NET_WM_WINDOW_TYPE_DESKTOP)||type[0]==Atom(_NET_WM_WINDOW_TYPE_DOCK))) {
                XRaiseWindow(x, id);
            }
        }
        XFlush(x);
    }

    int addTask(XID id) {
        XWindowAttributes wa; XGetWindowAttributes(x, id, &wa); if(wa.override_redirect||wa.map_state!=IsViewable) return -1;
        array<Atom> type=Window::getProperty<Atom>(id,"_NET_WM_WINDOW_TYPE");
        if(type.size()>=1 && type.first()!=Atom(_NET_WM_WINDOW_TYPE_NORMAL)) return -1;
        if(contains(Window::getProperty<Atom>(id,"_NET_WM_STATE"),Atom(_NET_WM_SKIP_TASKBAR))) return -1;
        string title = getTitle(id);
        if(!title) return -1;
        Image icon = getIcon(id);
        tasks << Task(id,move(icon),move(title));
        return tasks.array::size()-1;
    }

#if DBUS
    void registerStatusNotifierItem(string service) {
        for(const StatusNotifierItem& s: status) if(s.item.target == service) return;

        DBus::Object DBus = dbus("org.freedesktop.DBus/org/freedesktop/DBus"_);
        DBus.noreply("org.freedesktop.DBus.AddMatch"_,string("member='NameOwnerChanged',arg0='"_+service+"'"_));

        DBus::Object item(&dbus,move(service),"/StatusNotifierItem"_);
        Image icon;
        string name = item.get<string>("IconName"_);
        for(const string& folder: iconPaths) {
            string path = replace(folder,"$size"_,"16x16"_)+name+".png"_;
            if(exists(path)) { icon=resize(decodeImage(readFile(path)), 16,16); break; }
        }
        if(!icon) {
            array<DBusIcon> icons = item.get< array<DBusIcon> >("IconPixmap"_);
            assert(icons,name,"not found");
            DBusIcon dbusIcon = move(icons.first());
            icon=swap(resize(Image(cast<byte4>(move(dbusIcon.data)),dbusIcon.width,dbusIcon.height),16,16));
        }
        string id = item.get<string>("Id"_);
        status << StatusNotifierItem(move(item), move(icon), move(id));
        panel.update(); if(window.id) window.render();
    }
    variant<int> Get(string interface, string property) {
        if(interface=="org.kde.StatusNotifierWatcher"_ && property=="ProtocolVersion"_) return 1;
        else error(interface, property);
    }
    void removeStatusNotifierItem(string name, string, string) {
        for(uint i=0;i<status.array<StatusNotifierItem>::size();i++) if(status[i].item.target == name) status.removeAt(i);
        panel.update(); window.render();
    }
#endif

    TaskBar(array<string>&&) {
        x = XOpenDisplay(0);
        XSetWindowAttributes attributes; attributes.cursor=XCreateFontCursor(x,68);
        XChangeWindowAttributes(x,x->screens[0].root,CWCursor,&attributes);
        registerPoll(i({XConnectionNumber(x), POLLIN}));
        XSelectInput(x,x->screens[0].root,SubstructureNotifyMask|SubstructureRedirectMask|PropertyChangeMask|ButtonPressMask);
        for(XID id: getWindows()) {
            addTask(id);
            XSelectInput(x, id, StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
            XWindowAttributes wa; XGetWindowAttributes(x, id, &wa); if(wa.override_redirect) continue;
            XGrabButton(x,1,AnyModifier,id,0,ButtonPressMask,0,1,0,0);
        }
        XFlush(x);

#if DBUS
        if(dbus) {
            dbus.bind("Get"_,this,&TaskBar::Get);
            dbus.bind("RegisterStatusNotifierItem"_,this,&TaskBar::registerStatusNotifierItem);
            dbus.connect("NameOwnerChanged"_,this,&TaskBar::removeStatusNotifierItem);
            DBus::Object DBus = dbus("org.freedesktop.DBus/org/freedesktop/DBus"_);
            array<string> names = DBus("org.freedesktop.DBus.ListNames"_);
            for(string& name: names) if(startsWith(name,"org.kde.StatusNotifierItem"_)) registerStatusNotifierItem(move(name));
            DBus.noreply("org.freedesktop.DBus.RequestName"_, "org.kde.StatusNotifierWatcher"_, (uint)0);
        }
#endif

        start.image = resize(buttonIcon, 16,16);
        start.triggered.connect(this,&TaskBar::startButton);
        tasks.expanding=true;
        clock.timeout.connect(&window, &Window::render);
        clock.timeout.connect(&calendar, &Calendar::checkAlarm);
        calendar.eventAlarm.connect(&calendar,&Popup<Calendar>::toggle);
        clock.triggered.connect(&calendar,&Popup<Calendar>::toggle);

        window.setType(Atom(_NET_WM_WINDOW_TYPE_DOCK));
        window.setOverrideRedirect(true);
        window.setPosition(int2(0, 0));
        calendar.window.setOverrideRedirect(true);
        calendar.window.setPosition(int2(-300, 16));
        launcher.window.setPosition(int2(0, 16));
        window.show();
    }

    void event(pollfd) override {
        bool needUpdate = false;
        while(XPending(x)) { XEvent e; XNextEvent(x,&e); XID id = e.window;
            if(e.type==CreateNotify) {
                XSelectInput(x,id, StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
                XWindowAttributes wa; XGetWindowAttributes(x, id, &wa); if(wa.override_redirect) continue;
                XGrabButton(x,1,AnyModifier,id,0,ButtonPressMask,0,1,0,0);
                continue;
            } else if(e.type == MapRequest) {
                XMapWindow(x, id);
                raise(id);
                int i = indexOf(tasks, Task(id));
                if(i<0) { i=addTask(id); if(i>=0) tasksChanged.emit(tasks.array::size()); }
                if(i<0) continue;
                tasks.index=i;
            } else if(e.type == ConfigureRequest) {
                XConfigureRequestEvent& c = (XConfigureRequestEvent&)e;
                id = c.window;
                XWindowAttributes wa; XGetWindowAttributes(x, id, &wa);
                if(c.value_mask & CWX) wa.x=c.x; if(c.value_mask & CWY) wa.y=c.y;
                if(c.value_mask & CWWidth) wa.width=c.width; if(c.value_mask & CWHeight) wa.height=c.height;
                array<Atom> motif = Window::getProperty<Atom>(id,"_MOTIF_WM_HINTS");
                array<Atom> type = Window::getProperty<Atom>(id,"_NET_WM_WINDOW_TYPE");
                if(!wa.override_redirect &&
                        (!type || type[0]==Atom(_NET_WM_WINDOW_TYPE_NORMAL) || type[0]==Atom(_NET_WM_WINDOW_TYPE_DESKTOP))
                        && (!motif || motif[0]!=3 || motif[1]!=0)) {
                    wa.width=min(Window::screen.x,wa.width); wa.height=min(Window::screen.y-16,wa.height);
                    wa.x = (Window::screen.x - wa.width)/2;
                    wa.y = 16+(Window::screen.y-16-wa.height)/2;
                }
                XMoveResizeWindow(x,id, wa.x,wa.y,wa.width,wa.height);
                continue;
            } else if(e.type == ButtonPress) {
                raise(id);
                XAllowEvents(x, 2, 0);
                int i = indexOf(tasks, Task(id));
                if(i>=0) tasks.index=i;
            } else if(e.type==PropertyNotify) {
                int i = indexOf(tasks, Task(id));
                if(i<0) i=addTask(id);
                if(i<0) continue;
                Atom atom = ((XPropertyEvent&)e).atom;
                if(atom==Atom(_NET_WM_NAME)) tasks[i].get<Text>().setText( getTitle(id) );
                else if(atom==Atom(_NET_WM_ICON)) tasks[i].get<Icon>().image = getIcon(id);
                else continue;
            } else if(e.type == UnmapNotify || e.type == DestroyNotify || e.type == ReparentNotify || e.type == VisibilityNotify) {
                int i = indexOf(tasks, Task(id));
                if(i>=0) {
                    tasks.removeAt(i);
                    if(tasks.index == uint(i)) tasks.index=-1;
                    tasksChanged.emit(tasks.array::size());
                }

                array<XID> windows = getWindows();
                if(windows) {
                    XID id=windows.last();
                    //XSetInputFocus(x, id, RevertToPointerRoot, CurrentTime);
                    if(tasks.index==uint(-1)) tasks.setActive(indexOf(tasks, Task(id)));
                }
            } /*else if(e.type == ClientMessage) {
                if(e.xclient.message_type==Atom(_NET_ACTIVE_WINDOW)) {
                    XMapWindow(x, id);
                    raise(id);
                    int i = indexOf(tasks, Task(id));
                    if(i<0) { i=addTask(id); if(i>=0) tasksChanged.emit(tasks.array::size()); }
                    if(i<0) continue;
                    tasks.index=i;
                }
            }*/
            needUpdate = true;
        }
        if(needUpdate && window.visible) { panel.update(); window.render(); }
   }
};
Application(TaskBar)
