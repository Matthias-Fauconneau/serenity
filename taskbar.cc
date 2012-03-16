//TODO: Calendar, Events, Notifications, Jump Lists
#include "process.h"
#include "time.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "poll.h"
#include "array.cc"

#define DBUS 1
#if DBUS
#include "dbus.h"
struct Status : TriggerButton {
    DBus::Object item;
    Status(DBus::Object&& item, Image&& icon) : TriggerButton(move(icon)), item(move(item)) {}
    bool mouseEvent(int2 position, Event event, Button button) override {
        if(TriggerButton::mouseEvent(position,event,button)) { item.noreply("org.kde.StatusNotifierItem.Activate"_,0,0); return true; }
        return false;
    }
};
#endif

struct Task : Item {
    XID id;
    Task(XID id):id(id){} //for indexOf
    Task(XID id, Icon&& icon, Text&& text):Item(move(icon),move(text)),id(id){}
};
bool operator==(const Task& a,const Task& b){return a.id==b.id;}

struct Clock : Text, Timer {
    signal<> render;
    signal<> triggered;
    Clock():Text(date("hh:mm"_)){ setAbsolute(getUnixTime()/60*60+60); }
    void expired() { text=date("hh:mm"_); update(); setAbsolute(getUnixTime()+60); render.emit(); }
    bool mouseEvent(int2, Event event, Button button) override {
        if(event==Press && button==LeftButton) { triggered.emit(); return true; }
        return false;
    }
};

ICON(shutdown);

struct Desktop {
    Space space;
    List<Command> shortcuts { readShortcuts() };
    List<Command> system { Command(move(shutdownIcon),"Shutdown"_,"/sbin/poweroff"_) };
    HBox applets { &space, &shortcuts, &system };
    Window window{&applets,int2(-1,-1)};
    Desktop() { window.setType("_NET_WM_WINDOW_TYPE_DESKTOP"_); }
};

struct Calendar : Widget {
    Window window{this,int2(256,256)};
    array<Text> texts;
    Calendar() {
        window.setType("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"_);
        window.setOverrideRedirect(true);
    }
    void update() { //TODO: layout
        texts.clear();
        texts << Text(date("dddd, dd MMMM yyyy"_));
        texts.first().Widget::size=size;
    }
    void render(int2 parent) {
        texts.first().render(parent);
    }
    bool mouseEvent(int2, Event event, Button) {
        if(event==Leave) { window.hide(); return true; }
        return false;
    }
    void show() { window.show(); window.setPosition(int2(-size.x,0)); }
};

#define Atom(name) XInternAtom(x, #name, 1)

ICON(button);

struct TaskBar : Poll {
    Display* x;
    DBus dbus;
    signal<int> tasksChanged;

      TriggerButton start;
       Launcher launcher;
      Bar<Task> tasks;
      Bar<Status> status;
      Clock clock;
       Calendar calendar;
     HBox panel {&start, &tasks, &status, &clock };
    Window window{&panel,int2(-1,0),"TaskBar"_};

    template<class T> array<T> getProperty(XID window, const char* property) {
        Atom atom = XInternAtom(x,property,1);
        Atom type; int format; ulong size, bytesAfter; uint8* data =0;
        XGetWindowProperty(x,window,atom,0,~0,0,0,&type,&format,&size,&bytesAfter,&data);
        if(!data || !size) return array<T>();
        array<T> list = copy(array<T>((T*)data,size));
        assert(list.data()!=(T*)data);
        XFree(data);
        return list;
    }

    string getTitle(XID id) { return getProperty<char>(id,"_NET_WM_NAME"); }
    Image getIcon(XID id) {
        array<ulong> buffer = getProperty<ulong>(id,"_NET_WM_ICON");
        if(buffer.size()<=2) return Image();
        array<byte4> image(buffer.size());
        for(uint i=0;i<buffer.size()-2;i++) image << *(byte4*)&buffer[i+2];
        return resize(Image(move(image), buffer[0], buffer[1]), 16,16);
    }
    int addTask(XID id) {
        if(getProperty<Atom>(id,"_NET_WM_WINDOW_TYPE") != array<Atom>{Atom(_NET_WM_WINDOW_TYPE_NORMAL)}) return -1;
        if(contains(getProperty<Atom>(id,"_NET_WM_STATE"),Atom(_NET_WM_SKIP_TASKBAR))) return -1;
        string title = getTitle(id);
        Image icon = getIcon(id);
        if(!title) return -1;
        tasks << Task(id,move(icon),move(title));
        return tasks.array::size()-1;
    }

#if DBUS
    void registerStatusNotifierItem(string service) {
        for(const auto& s: status) if(s.item.target == service) return;
        DBus::Object item(&dbus,move(service),"/StatusNotifierItem"_);
        Image icon;
        string path = "/usr/share/icons/oxygen/16x16/apps/"_+item.get<string>("IconName"_)+".png"_;
        if(exists(path)) icon=Image(mapFile(path));
        else {
            DBusIcon dbusIcon = move(item.get< array<DBusIcon> >("IconPixmap"_).first());
            icon=swap(Image(cast<byte4>(move(dbusIcon.data)),dbusIcon.width,dbusIcon.height));
        }
        status << Status(move(item), resize(icon, 16,16));
        panel.update(); window.render();
    }
    variant<int> Get(string interface, string property) { //TODO: DBus properties
        if(interface=="org.kde.StatusNotifierWatcher"_ && property=="ProtocolVersion"_) return 1;
        else error(interface, property);
    }
#endif

    static int xErrorHandler(Display*, XErrorEvent*) { return 0; }
    TaskBar() {
        XSetErrorHandler(xErrorHandler);
        x = XOpenDisplay(0);
        XSetWindowAttributes attributes; attributes.cursor=XCreateFontCursor(x,68);
        XChangeWindowAttributes(x,DefaultRootWindow(x),CWCursor,&attributes);
        XSelectInput(x,DefaultRootWindow(x),SubstructureNotifyMask|PropertyChangeMask);
        registerPoll({XConnectionNumber(x), POLLIN});
        for(auto id: getProperty<XID>(DefaultRootWindow(x),"_NET_CLIENT_LIST")) {
           addTask(id);
           XSelectInput(x, id, PropertyChangeMask);
        }

#if DBUS
        dbus.bind("Get"_,this,&TaskBar::Get);
        dbus.bind("RegisterStatusNotifierItem"_,this,&TaskBar::registerStatusNotifierItem);
        DBus::Object DBus = dbus("org.freedesktop.DBus/org/freedesktop/DBus"_);
        array<string> names = DBus("org.freedesktop.DBus.ListNames"_);
        for(string& name: names) if(startsWith(name,"org.kde.StatusNotifierItem"_)) registerStatusNotifierItem(move(name));
        DBus("org.freedesktop.DBus.RequestName"_, "org.kde.StatusNotifierWatcher"_, (uint)0);
#endif

         start.image = resize(buttonIcon, 16,16);
         start.triggered.connect(&launcher,&Launcher::show);
         tasks.expanding=true;
         tasks.activeChanged.connect(this,&TaskBar::raise);
         clock.render.connect(&window, &Window::render);
         clock.triggered.connect(&calendar,&Calendar::show);
        panel.update();

        window.setType("_NET_WM_WINDOW_TYPE_DOCK"_);
        window.show();
        window.setPosition(int2(0,0));
        window.update();
    }
    void raise(int) {
        XSetInputFocus(x, tasks.active().id, RevertToNone, CurrentTime);
        XEvent xev; clear(xev);
        xev.type = ClientMessage;
        xev.xclient.window = tasks.active().id;
        xev.xclient.message_type = Atom(_NET_ACTIVE_WINDOW);
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = 2;
        xev.xclient.data.l[1] = 0;
        xev.xclient.data.l[2] = 0;
        XSendEvent(x, DefaultRootWindow(x), 0, SubstructureNotifyMask, &xev);
        XFlush(x);
    }
    void event(pollfd) {
        bool needUpdate = false;
        while(XEventsQueued(x, QueuedAfterFlush)) {
            XEvent e; XNextEvent(x,&e);
            //TODO: receive only useful events
            XID id = (e.type==PropertyNotify||e.type==ClientMessage) ? e.xproperty.window : e.xconfigure.window;
            if(e.type==PropertyNotify && id==DefaultRootWindow(x) && e.xproperty.atom == Atom(_NET_ACTIVE_WINDOW)) {
                XID id = getProperty<XID>(DefaultRootWindow(x),"_NET_ACTIVE_WINDOW").first();
                int i = indexOf(tasks, Task(id));
                if(i<0) i=addTask(id);
                if(i>=0) {
                    if((!tasks[i].text.text || !tasks[i].icon.image)) tasks.removeAt(i);
                    else tasks.index=i;
                    needUpdate = true;
                }
            } else {
                int i = indexOf(tasks, Task(id));
                if(e.type == CreateNotify || e.type==ReparentNotify) XSelectInput(x, id, StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
                if(e.type == PropertyNotify) {
                    if(e.xproperty.atom==Atom(_NET_WM_NAME)) {
                        if(i<0) i=addTask(id);
                        else tasks[i].text.text = getTitle(id);
                        needUpdate = true;
                    }
                    if(i>=0 && e.xproperty.atom==Atom(_NET_WM_ICON)) tasks[i].icon = getIcon(id), needUpdate = true;
                    if(i>=0 && e.xproperty.atom==Atom(_NET_WM_WINDOW_TYPE) &&
                            getProperty<Atom>(id,"_NET_WM_WINDOW_TYPE") != array<Atom>{Atom(_NET_WM_WINDOW_TYPE_NORMAL)})  tasks.removeAt(i);
                }
                if(i>=0 && (e.type == DestroyNotify || e.type == UnmapNotify || e.type==ReparentNotify || (!tasks[i].text.text || !tasks[i].icon.image))) {
                    tasks.removeAt(i), needUpdate = true;
                }
            }
        }
        if(needUpdate) {
            panel.update(); window.render();
            tasksChanged.emit(tasks.array::size());
        }
   }
};

struct Shell : Application {
    TaskBar taskbar;
    Desktop desktop;
    Shell() {
        taskbar.tasksChanged.connect(this,&Shell::tasksChanged);
        tasksChanged(taskbar.tasks.array::size());
    }
    void tasksChanged(int count) { taskbar.window.setVisible(count); desktop.window.setVisible(!count); }
} shell;
