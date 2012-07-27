#pragma once
#include "window.h"
#include "interface.h"
#include "html.h"
#include "file.h"

struct Entry : Item {
    bool isHeader=false;
    string link;
    Entry(string&& name, string&& link, Image<byte4>&& icon=Image<byte4>()):Item(move(icon),move(name)),link(move(link)){}
};

struct Feeds : List<Entry> {
    int readConfig;
    Map readMap;
    signal<> listChanged;
    signal< const ref<byte>& > pageChanged;

    Feeds();
    ~Feeds();

    bool isRead(const Entry& entry);
    void setRead(const Entry& entry);
    void setAllRead();
    void loadFeed(const URL&, array<byte>&& document);
    void getFavicon(const URL& url, array<byte>&& document);
    void activeChanged(int index);
    void itemPressed(int index);
    void readNext();
};
