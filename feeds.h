#pragma once
#include "window.h"
#include "interface.h"
#include "html.h"
#include "file.h"

struct Entry : Item {
    bool isHeader=false;
    string link;
    Scroll<HTML>* content=0;
    Entry(Entry&& o);
    Entry(string&& name, string&& link, Image<byte4>&& icon=Image<byte4>());
    ~Entry();
};

struct Feeds : List<Entry> {
    int readConfig;
    Map readMap;
    signal<> contentChanged;
    Scroll<HTML>* content=0;
    Window window {0}; //keep the same window to implement \a nextItem

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
