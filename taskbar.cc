//TODO: Calendar, Notifications, Jump Lists
#include "process.h"
#include "dbus.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"

//-> time.h
#include "time.h"
time_t unixTime() { timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec; }
string date(const string& format) {
    time_t time=unixTime(); tm date; localtime_r(&time,&date);
    string r(format.size);
    for(Stream<> s(format);s;) {
        /**/ if(s.match("ss"_))  r << toString(date.tm_sec,10,2);
        else if(s.match("mm"_))  r << toString(date.tm_min,10,2);
        else if(s.match("hh"_))  r << toString(date.tm_hour,10,2);
        else if(s.match("d"_))   r << toString(date.tm_mday);
        else if(s.match("MM"_))  r << toString(date.tm_mon,10,2);
        else if(s.match("yyyy"_))r << toString(date.tm_year);
        else r << s.read();
    }
    return r;
}

#include <sys/timerfd.h>
struct Timer : Poll {
    int fd = timerfd_create(CLOCK_REALTIME,0);
    signal<> trigger;
    Timer(){ registerPoll(); }
    void setAbsolute(int date) { itimerspec timer{timespec{0,0},timespec{date,0}}; timerfd_settime(fd,TFD_TIMER_ABSTIME,&timer,0); };
    pollfd poll() { return {fd, POLLIN}; }
    virtual void expired() =0;
    void event(pollfd) { expired(); trigger.emit(); }
};

ICON(button);

#define Font XWindow
#define Window XWindow
#define Status XStatus
#include <X11/Xlib.h>
#undef Window
#undef Font
#undef Status

#define Atom(name) XInternAtom(x, #name, 1)

struct Task : Tab {
    XWindow id;
    Task(XWindow id):id(id){}
};

struct Status : TriggerButton {
    DBus::Object app;
    Status(DBus::Object&& app, const Image& icon) : TriggerButton(icon), app(move(app)) {}
    bool mouseEvent(int2 position, Event event, Button button) override {
        if(TriggerButton::mouseEvent(position,event,button)) { app("org.kde.StatusNotifierItem.Activate"_,0,0); return true; }
        return false;
    }
};

struct Clock : Text, Timer {
    Clock():Text(date("hh:mm"_)){ setAbsolute(unixTime()/60*60+60); }
    void expired() { text=date("hh:mm"_); update(); setAbsolute(unixTime()+60); }
};

struct TaskBar : Poll, Application {
    Display* x;
    DBus dbus;

    template<class T> array<T> getProperty(XWindow window, const char* property) {
        Atom atom = XInternAtom(x,property,1);
        Atom type; int format; ulong size, bytesAfter; uint8* data =0;
        XGetWindowProperty(x,window,atom,0,~0,0,0,&type,&format,&size,&bytesAfter,&data);
        if(!data || !size) return array<T>();
        array<T> list = array<T>((T*)data,size).copy();
        XFree(data);
        return list;
    }

    HBox panel;
    Window window = Window(panel,int2(0,16),"TaskBar"_);
    TriggerButton start;
    Launcher launcher;
    Bar<Task> tasks;
    Bar<Status> status;
    Clock clock;

    Task* addTask(XWindow w) {
        if(getProperty<Atom>(w,"_NET_WM_WINDOW_TYPE") != array<Atom>{Atom(_NET_WM_WINDOW_TYPE_NORMAL)}) return 0;
        XSelectInput(x,w,PropertyChangeMask);
        Task task(w);
        updateTask(task);
        if(!task.text.text || !task.icon.image) return 0;
        tasks << move(task);
        return &tasks.last();
    }
    void updateTask(Task& task) {
        task.text.text = getProperty<char>(task.id,"_NET_WM_NAME");
        array<int> buffer = (array<int>)getProperty<ulong>(task.id,"_NET_WM_ICON");
        if(sizeof(long)==8) for(int i=0;i<buffer.size/2;i++) buffer[i]=buffer[2*i]; //discard CARDINAL high words
        if(buffer.size>2) task.icon = Image((array<byte4>)buffer.slice(2).copy(),buffer[0],buffer[1]).resize(16,16);
    }

    void updateStatusNotifierItems() {
        status.clear();
        DBus::Object StatusNotifierWatcher = dbus("org.kde.StatusNotifierWatcher/StatusNotifierWatcher"_);
        StatusNotifierWatcher("org.kde.StatusNotifierWatcher.RegisterStatusNotifierHost"_, dbus.name);
        Variant<array<string>> items = StatusNotifierWatcher["RegisteredStatusNotifierItems"_];
        for(const string& item: items) {
            Image icon;
            string path = "/usr/share/icons/oxygen/16x16/apps/"_+(Variant<string>)dbus(item)["IconName"_]+".png"_;
            if(exists(path)) icon=Image(mapFile(path));
            else {
                DBusIcon dbusIcon = move(((Variant<array<DBusIcon>>)dbus(item)["IconPixmap"_]).first());
                icon=move(Image((array<byte4>)move(dbusIcon.data),dbusIcon.width,dbusIcon.height).swap());
            }
            status << Status(dbus(item), icon.resize(16,16));
        }
    }

    TaskBar() {
        x = XOpenDisplay(0);
        registerPoll();
        XSelectInput(x,DefaultRootWindow(x),SubstructureNotifyMask|PropertyChangeMask);
        for(auto w: getProperty<XWindow>(DefaultRootWindow(x),"_NET_CLIENT_LIST")) addTask(w);

        DBus::Object DBus = dbus("org.freedesktop.DBus/org/freedesktop/DBus"_);
        DBus("org.freedesktop.DBus.AddMatch"_, "type='signal',sender='org.kde.StatusNotifierWatcher',interface='org.kde.StatusNotifierWatcher',"
             "path='/StatusNotifierWatcher',member='StatusNotifierItemRegistered'"_);
        dbus.signals["StatusNotifierItemRegistered"_].connect(this,&TaskBar::updateStatusNotifierItems);
        DBus("org.freedesktop.DBus.AddMatch"_, "type='signal',sender='org.kde.StatusNotifierWatcher',interface='org.kde.StatusNotifierWatcher',"
             "path='/StatusNotifierWatcher',member='StatusNotifierItemUnregistered'"_);
        dbus.signals["StatusNotifierItemUnregistered"_].connect(this,&TaskBar::updateStatusNotifierItems);

        panel << start << tasks << status << clock;
         start.image = buttonIcon.resize(16,16);
         start.triggered.connect(&launcher,&Launcher::show);
         tasks.expanding=true;
         tasks.activeChanged.connect(this,&TaskBar::raise);
         clock.trigger.connect(&window, &Window::render);
        panel.update();

        window.keyPress.connect(this, &TaskBar::keyPress);
        window.setType("_NET_WM_WINDOW_TYPE_DOCK"_);
        window.render(); window.show(); window.move(int2(0,0)); window.sync();
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
    pollfd poll() { return {XConnectionNumber(x), POLLIN}; }
    void event(pollfd) {
        bool needUpdate = false;
        while(XEventsQueued(x, QueuedAfterFlush)) { XEvent e; XNextEvent(x,&e);
            //TODO: try to optimize by receiving only useful events
            XWindow id = (e.type==PropertyNotify||e.type==ClientMessage) ? e.xproperty.window : e.xconfigure.window;
            Task* task = tasks.find([id](const Task& t){return t.id==id;});
            if(!task && (e.type == CreateNotify || e.type == MapNotify || e.type==ReparentNotify)) task=addTask(id);
            if(!task) continue;
            /**/ if(e.type == CreateNotify || e.type == MapNotify || e.type == ReparentNotify) {
            } else if(e.type == ConfigureNotify || e.type == ClientMessage ||
                      (e.type==PropertyNotify && e.xproperty.atom != Atom(_NET_WM_NAME) && e.xproperty.atom != Atom(_NET_WM_ICON))) {
                updateTask(*task);
            } else if(e.type == DestroyNotify || e.type == UnmapNotify) {
                tasks.removeRef(task);
            } else continue;
            needUpdate = true;
        }
        if(needUpdate) { panel.update(); window.render(); }
    }
    void keyPress(Key key) { if(key==Escape) quit(); }
} taskbar;
