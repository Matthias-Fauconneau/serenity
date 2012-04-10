#pragma once
#include "interface.h"
#include "window.h"
#include "html.h"

struct Entry : Item {
    bool isHeader=false;
    string link;
    Scroll<HTML>* content=new Scroll<HTML>;
    Entry(Entry&& o);
    Entry(string&& name, string&& link, Image&& icon=Image());
    ~Entry();
};

struct Feeds : List<Entry> {
    int readConfig;
    array<string> read;
    signal<> contentChanged;
    Window window {0}; //keep the same window to implement \a nextItem

    Feeds();
    ~Feeds();

private:
    bool isRead(const Entry& entry);
    void setRead(const Entry& entry);
    void loadFeed(const URL&, array<byte>&& document);
    void getFavicon(const URL& url, array<byte>&& document);
    void activeChanged(int index);
    void itemPressed(int index);
    void readNext();
};
