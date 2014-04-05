#include "thread.h"

#include "dbus.h"
#include <unistd.h>
struct StatusNotifierItem : DBus {
    DBus::Object dbus{this, String("org.freedesktop.DBus"_),String("/org/freedesktop/DBus"_)};
    StatusNotifierItem() {
        String name = "org.kde.StatusNotifierItem-"_+dec(getpid())+"-1"_;
        dbus("RequestName"_, name, (uint)0);
        DBus::Object item{this, String("org.kde.StatusNotifierItem"_), copy(name)};
        item.set("Category"_,variant<String>(String("ApplicationStatus"_)));
        item.set("Id"_,variant<String>(String("Player"_)));
        item.set("Title"_,variant<String>(String("Player"_)));
        item.set("IconName"_,variant<String>(String("media-playback-start"_)));
        //item.set("Status"_,variant<String>(String("Active"_)));
        //item.set("WindowId"_,variant<uint>(window.id));
        //TODO: VOID org.freedesktop.StatusNotifierItem.Activate (INT x, INT y);
        //TODO: signals
        //exit();
    }
} test;
