//TODO: [backup], Application Launchers, Calendar, Notifications, advanced StatusNotifierItem
#include "process.h"
#include "dbus.h"
#include "interface.h"

//->window.h
#define Font XWindow
#define Window XWindow
#define Status XStatus
#include <X11/Xlib.h>
#undef Window
#undef Font
#undef Status

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

struct Shell : Poll, Application {
    Display* x;
    DBus dbus;

    template<class T> array<T> getProperty(XWindow window, const char* property) {
        Atom atom = XInternAtom(x,property,1);
        Atom type; int format; ulong size, bytesAfter; uint8* data =0;
        XGetWindowProperty(x,window,atom,0,~0,0,0,&type,&format,&size,&bytesAfter,&data);
        if(!data || !size) return array<T>();
        array<T> list((T*)data,size); list.detach();
        XFree(data);
        return list;
    }

    struct Task : Tab {
        XWindow id;
        Task(XWindow id):id(id){}
    };

    struct Status : Icon {
        DBus::Object app;
        Status(DBus::Object&& app, const string& icon)
            : Icon(mapFile("/usr/share/icons/oxygen/16x16/apps/"_+icon+".png"_)), app(move(app)) {}
        bool event(int2 position, Event event, State state) override {
            if(Icon::event(position,event,state)) { app("org.kde.StatusNotifierItem.Activate"_,0,0); return true; }
            return false;
        }
    };

    struct Clock : Text, Timer {
        Clock():Text(16,date("hh:mm"_)){ setAbsolute(unixTime()/60*60+60); }
        void expired() { setText(date("hh:mm"_)); /*window.render();*/ setAbsolute(unixTime()+60); }
    };

    HBox panel;
    Window window = Window(panel,int2(0,16),"Shell"_);
    Bar<Task> tasks;
    Bar<Status> status;
    Clock clock;

    #define Atom(name) XInternAtom(x, #name, 1)
    Task* addTask(XWindow w) {
        if(getProperty<Atom>(w,"_NET_WM_WINDOW_TYPE") != array<Atom>{Atom(_NET_WM_WINDOW_TYPE_NORMAL)}) return 0;
        Task task(w);
        updateTask(task);
        if(!task.text.text) return 0;
        tasks << move(task);
        return &tasks.last();
    }
    void updateTask(Task& task) {
        task.text.setText(getProperty<char>(task.id,"_NET_WM_NAME"));
        array<int> buffer = (array<int>)getProperty<ulong>(task.id,"_NET_WM_ICON");
        if(sizeof(long)==8) for(int i=0;i<buffer.size/2;i++) buffer[i]=buffer[2*i]; //discard CARDINAL high words
        if(buffer.size>2) task.icon = Image((array<byte4>)buffer.slice(2).copy(),buffer[0],buffer[1]);
    }

    Shell() {
        window.keyPress.connect(this, &Shell::keyPress);
        clock.trigger.connect(&window, &Window::render);

        x = XOpenDisplay(0);
        registerPoll();
        XSelectInput(x,DefaultRootWindow(x),SubstructureNotifyMask|PropertyChangeMask);

        for(auto w: getProperty<XWindow>(DefaultRootWindow(x),"_NET_CLIENT_LIST")) addTask(w);

        DBus::Object StatusNotifierWatcher = dbus("org.kde.StatusNotifierWatcher/StatusNotifierWatcher"_);
        StatusNotifierWatcher("org.kde.StatusNotifierWatcher.RegisterStatusNotifierHost"_, dbus.name);
        Variant<array<string>> items = StatusNotifierWatcher["RegisteredStatusNotifierItems"_];
        for(const string& item: items) {
            string icon = (Variant<string>)dbus(item)["IconName"_];
            status << Status(dbus(item),icon);
        }

        window.setProperty<uint>("ATOM", "_NET_WM_WINDOW_TYPE", {(uint)Atom(_NET_WM_WINDOW_TYPE_DOCK),0});
        panel << tasks << status << clock;
         tasks.expanding=true;
         tasks.activeChanged.connect(this,&Shell::raise);
        panel.update();
        window.setVisible(true);
    }
    void raise(int) {
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
        while(XEventsQueued(x, QueuedAfterFlush)) { XEvent e; XNextEvent(x,&e);
            XWindow id = (e.type==PropertyNotify||e.type==ClientMessage) ? e.xproperty.window : e.xconfigure.window;
            if(id == window.id) continue;
            Task* task = tasks.find([id](const Task& t){return t.id==id;});
            if(!task && (e.type == ReparentNotify || !(task=addTask(id)))) continue;
            /**/ if(e.type == CreateNotify || e.type == MapNotify || e.type == ReparentNotify) {}
            else if(e.type == PropertyNotify || e.type == ConfigureNotify || e.type == ClientMessage) updateTask(*task);
            else if(e.type == DestroyNotify || e.type == UnmapNotify) tasks.removeRef(task);
            else continue;
            panel.update(); window.render();
        }
    }
    void keyPress(Event key) { if(key==Quit) Application::running=false; }
} shell;
