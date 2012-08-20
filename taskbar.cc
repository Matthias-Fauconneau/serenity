#include "process.h"
#include "file.h"
#include "time.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "calendar.h"
#include "x.h"
#include "linux.h"

ICON(cursor)
ICON(button)
struct Taskbar : Application, Poll {
    struct Task : Item {
        Taskbar* parent=0;
        uint id;
        Task(uint id):id(id){} //for indexOf
        Task(Taskbar* parent, uint id, Image<byte4>&& icon, string&& text):Linear(Left),Item(move(icon),move(text)),parent(parent),id(id){}
        bool selectEvent() override { parent->raise(id); return true; }
        bool mouseEvent(int2, int2, Event event, Button button) override {
            //TODO: preview on hover
            if(button!=LeftButton) return false;
            if(event==Press && parent->getProperty<uint>(parent->root,"_NET_ACTIVE_WINDOW"_)==ref<uint>__(id)) { //Set Maximized
                {MoveResizeWindow r; r.id=id; r.x=0, r.y=16; r.w=display.x; r.h=display.y-16; write(parent->x,raw(r));}
                return true;
            }
            if(event==Motion) { //Set windowed
                {MoveResizeWindow r; r.id=id; r.x=display.x/4, r.y=16+(display.y-16)/4; r.w=display.x/2; r.h=(display.y-16)/2; write(parent->x,raw(r));}
                return true;
            }
            return false;
        }
    };

    const int x;
    bool ownWM=true;
    array<uint> windows;

    TriggerButton start __(resize(buttonIcon(), 16,16));
    Launcher launcher;
    Bar<Task> tasks;
    Clock clock __(16);
    Calendar calendar;
    Window popup __(&calendar.layout, int2(-256,-256));
    HBox panel __(&start, &tasks, &clock);
    Window window __(&panel,int2(0,16));
    uint root=window.root;
    uint desktop=0;

    Taskbar() : x(socket(PF_LOCAL, SOCK_STREAM, 0)) {
        string path = "/tmp/.X11-unix/X"_+getenv("DISPLAY"_).slice(1);
        sockaddr_un addr; copy(addr.path,path.data(),path.size());
        check_(connect(x,(sockaddr*)&addr,2+path.size()),path);
        {ConnectionSetup r;
            string authority = getenv("HOME"_)+"/.Xauthority"_;
            if(exists(authority)) write(x, string(raw(r)+readFile(authority).slice(18,align(4,(r.nameSize=18))+(r.dataSize=16))));
            else write(x, raw(r)); }
        {ConnectionSetupReply r=read<ConnectionSetupReply>(x); assert(r.status==1);
            read(x,r.additionnal*4-(sizeof(ConnectionSetupReply)-8)); }

        if(getProperty<uint>(root,"_NET_SUPPORTING_WM_CHECK"_)) ownWM=false;
        else window.setCursor(share(cursorIcon()), root);
        {SetWindowEventMask r; r.window=root; r.eventMask=SubstructureNotifyMask|PropertyChangeMask|ButtonPressMask;
            if(ownWM) r.eventMask|=SubstructureRedirectMask; write(x, raw(r)); }
        array<uint> windows;
        if(ownWM) {
            {QueryTree r; r.id=root; write(x, raw(r));}
            {QueryTreeReply r = readReply<QueryTreeReply>(); windows=read<uint>(x, r.count);}
        } else windows=getProperty<uint>(root,"_NET_CLIENT_LIST"_);
        for(uint id: windows) {
            addWindow(id);
            {SetWindowEventMask r; r.window=id; r.eventMask=StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask;
                write(x, raw(r));}
            if(ownWM){GrabButton r; r.window=id; write(x, raw(r));}
        }
        registerPoll(__(x, POLLIN));

        start.triggered.connect(this,&Taskbar::startButton);
        launcher.window.setPosition(int2(0, 0));
        tasks.expanding=true;
        clock.timeout.connect(&window, &Window::render);
        clock.timeout.connect(&calendar, &Calendar::checkAlarm);
        clock.triggered.connect(&calendar,&Calendar::reset);
        clock.triggered.connect(&popup,&Window::toggle);
        calendar.eventAlarm.connect(&popup,&Window::show);
        popup.hideOnLeave = true;
        popup.setPosition(int2(-popup.size.x, window.size.y));
        window.show();
    }

    void processEvent(uint8 type, const Event& event) {
        if(type==0) { const Error& e=(Error&)event;
            trace(); log("Error",e.code<sizeof(xerror)/sizeof(*xerror)?xerror[e.code]:dec(e.code),"seq:",e.seq,"id",e.id,"request",
                  e.major<sizeof(xrequest)/sizeof(*xrequest)?xrequest[e.major]:dec(e.major),"minor",e.minor);
        } else if(type==1) { error("Unexpected reply");
        } else { const Event& e=event; type&=0b01111111; //msb set if sent by SendEvent
            if(type==CreateNotify) { uint id=e.create.window;
                if(e.create.override_redirect) return;
                log(id);
                {SetWindowEventMask r; r.window=id; r.eventMask=StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask;
                    write(x, raw(r));}
                if(ownWM){GrabButton r; r.window=id; write(x, raw(r));}
                return;
            } else if(type == MapRequest) { uint id=e.map_request.window;
                {MapWindow r; r.id=id; write(x,raw(r));}
                raise(id);
                int i = tasks.indexOf(id);
                if(i<0) i=addWindow(id);
                if(i<0) return;
                tasks.index=i;
            } else if(type == ConfigureRequest) { uint id = e.configure_request.window;
                {GetGeometry r; r.id=id; write(x, raw(r));}
                GetGeometryReply w=readReply<GetGeometryReply>();
                const auto& c = e.configure_request;
                if(c.valueMask & X) w.x=c.x; if(c.valueMask & Y) w.y=c.y; if(c.valueMask & W) w.w=c.w; if(c.valueMask & H) w.h=c.h;
                array<uint> motif = getProperty<uint>(id,"_MOTIF_WM_HINTS"_), type = getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_);
                GetWindowAttributes r; r.window=id; write(x, raw(r)); GetWindowAttributesReply wa = readReply<GetWindowAttributesReply>();
                if(!wa.overrideRedirect && (!type || type[0]==Atom("_NET_WM_WINDOW_TYPE_NORMAL"_)) && (!motif || motif[0]!=3 || motif[1]!=0)) {
                    w.w=min<int16>(display.x,w.w); w.h=min<int16>(display.y-16,w.h);
                    w.x = (display.x - w.w)/2; w.y = 16+(display.y-16-w.h)/2;
                }
                {MoveResizeWindow r; r.id=id; r.x=w.x, r.y=w.y; r.w=w.w; r.h=w.h; write(x, raw(r));}
                return;
            } else if(type == ButtonPress) { uint id = e.event;
                raise(id);
                write(x, raw(AllowEvents()));
                int i = tasks.indexOf(id);
                if(i>=0) tasks.index=i;
            } else if(type==PropertyNotify) { uint id=e.property.window;
                if(id==root && e.property.atom==Atom("_NET_ACTIVE_WINDOW"_)) {
                    array<uint> active = getProperty<uint>(root,"_NET_ACTIVE_WINDOW"_);
                    if(active) { int i = tasks.indexOf(active.first()); if(i<0) return; tasks.setActive(i); }
                } else if(id==root && e.property.atom==Atom("_NET_CLIENT_LIST"_)) {
                     windows.clear(); tasks.clear();
                     for(uint id: getProperty<uint>(root,"_NET_CLIENT_LIST"_)) {
                         addWindow(id);
                         {SetWindowEventMask r; r.window=id; r.eventMask=StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask;
                             write(x, raw(r));}
                         if(ownWM){GrabButton r; r.window=id; write(x, raw(r));}
                     }
                } else if(e.property.atom==Atom("_NET_WM_NAME"_)) {
                    int i = tasks.indexOf(id); /*if(i<0) i=addWindow(id);*/ if(i<0) return;
                    tasks[i].get<Text>().setText( getTitle(id) );
                } else if(e.property.atom==Atom("_NET_WM_ICON"_)) {
                    int i = tasks.indexOf(id); /*if(i<0) i=addWindow(id);*/ if(i<0) return;
                    tasks[i].get<Icon>().image = getIcon(id);
                } else return;
            } else if(type == DestroyNotify) { uint id=e.property.window;
                int i = tasks.indexOf(id);
                if(i>=0) {
                    tasks.removeAt(i);
                    if(tasks.index == uint(i)) tasks.index=-1;
                }
                if(windows) {
                    uint id=windows.last();
                    //{SetInputFocus r; r.window=id; write(x, raw(r));}
                    if(tasks.index==uint(-1)) tasks.setActive(tasks.indexOf(id));
                }
            } else if(type==MapNotify||type==UnmapNotify||type==ConfigureNotify||type==ClientMessage||type==ReparentNotify) {
            } else log("Event", type<sizeof(xevent)/sizeof(*xevent)?xevent[type]:str(type));
            window.render();
        }
    }

    /// Adds \a id to \a windows and to \a tasks if necessary
    int addWindow(uint id) {
        if(windows.contains(id)) return -1;
        windows << id;
        if(getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_)==ref<uint>__(Atom("_NET_WM_WINDOW_TYPE_DESKTOP"_))) desktop=id;
        if(id==root) return -1;
        {GetWindowAttributes r; r.window=id; write(x, raw(r)); GetWindowAttributesReply wa = readReply<GetWindowAttributesReply>();
            if(wa.overrideRedirect||wa.mapState==IsUnviewable) return -1;}
        array<uint> type = getProperty<uint>(id,"_NET_WM_WINDOW_TYPE"_);
        if(type.size()>=1 && type.first()!=Atom("_NET_WM_WINDOW_TYPE_NORMAL"_)) return -1;
        if(getProperty<uint>(id,"_NET_WM_STATE"_).contains(Atom("_NET_WM_SKIP_TASKBAR"_))) return -1;
        string title = getTitle(id); if(!title) return -1;
        Image<byte4> icon = getIcon(id);
        tasks << Task(this,id,move(icon),move(title));
        return tasks.array::size()-1;
    }

    string getTitle(uint id) {
        string name = getProperty<byte>(id,"_NET_WM_NAME"_);
        if(!name) name = getProperty<byte>(id,"WM_NAME"_);
        return move(name);
    }
    Image<byte4> getIcon(uint id) {
        array<uint> buffer = getProperty<uint>(id,"_NET_WM_ICON"_,2+128*128);
        if(buffer.size()<2) return Image<byte4>();
        uint w=buffer[0], h=buffer[1];
        if(buffer.size()<2+w*h) return Image<byte4>();
        return resize(Image<byte4>(array<byte4>(cast<byte4>(buffer.slice(2,w*h))),w,h,true), 16, 16);
    }

    map<string, uint> cache;
    uint Atom(const ref<byte>& name) {
        uint& atom = cache[string(name)];
        if(!atom) {
            {InternAtom r; r.length=name.size; r.size+=align(4,r.length)/4; write(x, string(raw(r)+name+pad(4,r.length)));}
            {InternAtomReply r=readReply<InternAtomReply>(); atom=r.atom;}
        }
        return atom;
    }
    template<class T> array<T> getProperty(uint window, const ref<byte>& name, uint size=128*128+2) {
        {GetProperty r; r.window=window; r.property=Atom(name); r.length=size; write(x, raw(r));}
        {GetPropertyReply r=readReply<GetPropertyReply>(); int size=r.length*r.format/8;
            array<T> a; if(size) a=read<T>(x,size/sizeof(T)); int pad=align(4,size)-size; if(pad) read(x, pad); return a; }
    }

    void raise(uint id) {
        if(ownWM) {
            {RaiseWindow r; r.id=id; write(x, raw(r));}
            //{SetInputFocus r; r.window=id; write(x, raw(r));}
            for(uint w: windows) {
                if(getProperty<uint>(w,"WM_TRANSIENT_FOR"_) == ref<uint>__(id)) {
                    {RaiseWindow r; r.id=id; write(x, raw(r));}
                    //{SetInputFocus r; r.window=id; write(x, raw(r));}
                }
            }
        } else {
            {SendEvent r; r.window=root; r.type=ClientMessage; r.eventMask=SubstructureNotifyMask;
                auto& e=r.event.client; e.format=32; e.window=id; e.type=Atom("_NET_ACTIVE_WINDOW"_); clear(e.data); e.data[0]=2;
                write(x, raw(r));}
        }
    }

    void startButton() { // Opens launcher or raise desktop on second click
        if(!launcher.window.mapped) launcher.window.show();
        else {
            launcher.window.hide();
            if(tasks.index<tasks.count()) { tasks.setActive(-1); window.render(); }
            if(desktop) raise(desktop);
        }
    }

    struct QEvent { uint8 type; Event event; } packed;
    array<QEvent> queue;
    void event(const pollfd& poll) {
        assert(poll.fd==x);
        uint8 type = read<uint8>(x);
        processEvent(type, read<Event>(x));
        while(queue) { QEvent e=queue.takeFirst(); processEvent(e.type, e.event); }
    }
    template<class T> T readReply(bool ignore=false) {
        for(;;) { uint8 type = read<uint8>(x);
            if(type==0) { ::Event e=read<::Event>(x); if(!ignore) processEvent(0,e); T t; clear(t); return t; }
            else if(type==1) return read<T>(x);
            else queue << QEvent __(type, read<::Event>(x)); //queue events to avoid reentrance
        }
    }
};
bool operator==(const Taskbar::Task& a,const Taskbar::Task& b){return a.id==b.id;}
Application(Taskbar)
