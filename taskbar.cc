#include "process.h"
#include "file.h"
#include "time.h"
#include "interface.h"
#include "window.h"
#include "launcher.h"
#include "calendar.h"
#include "popup.h"
#include "array.cc"

struct Task : Item {
    uint id;
    Task(uint id):id(id){} //for indexOf
    Task(uint id, Icon&& icon, Text&& text):Item(move(icon),move(text)),id(id){}
};
bool operator==(const Task& a,const Task& b){return a.id==b.id;}

ICON(button);
struct TaskBar : Application, Poll {
    int wm;

    signal<int> tasksChanged;

      TriggerButton start;
       Launcher launcher;
      Bar<Task> tasks;
      Clock clock;
       Popup<Calendar> calendar;
       HBox panel i({&start, &tasks, &clock });
      Window window{&panel,int2(0,-1)};
      //TODO: read title,icon from window

    TaskBar(array<string>&&) {
        //wm = socket
        registerPoll(pollfd i({wm,POLLIN}));

        start.image = resize(buttonIcon, 16,16);
        start.triggered.connect(this,&TaskBar::startButton);
        tasks.expanding=true;
        clock.timeout.connect(&window, &Window::render);
        clock.timeout.connect(&calendar, &Calendar::checkAlarm);
        calendar.eventAlarm.connect(&calendar,&Popup<Calendar>::toggle);
        clock.triggered.connect(&calendar,&Popup<Calendar>::toggle);

        calendar.window.setPosition(int2(-300, 16));
        launcher.window.setPosition(int2(0, 16));
        window.show();
    }

    void event(pollfd) {
        for(window w;read(wm, &w, sizeof(w)) > 0;) {
            read(wm, w.size-32); //TODO: read title and icon, add tasks as needed
        }
    }

    void startButton() {
        if(!launcher.window.shown) { launcher.show(); return; }
        // show desktop
        tasks.setActive(-1); raise(0);
    }

    /// Emits a size change (0=hide) for a window.
    void emit(uint id, int2 position, int2 size) {
        if(id==0) return;
        Window::window w = Window::windows[id];
        w.position=position; w.size=size;
        write(Window::wm,raw(w));
    }
    void hide(uint id) { emit(id,int2(0,0),int2(0,0)); }
    void show(uint id) { emit(current=id,int2(0,16),int2(display.width,display.height-16)); }
    void raise(uint id) { hide(current); show(id); }
    void raiseTask(int task) { return raise(tasks[task].id); }
};
Application(TaskBar)
