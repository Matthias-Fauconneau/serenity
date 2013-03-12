#pragma once
/// \file feeds.h RSS/Atom feeds reader (Entry, Favicon, Feeds)
#include "interface.h"
#include "html.h"
#include "file.h"
#include "ico.h"

/// Image shared between several \link Entry Entries\endlink
struct Favicon {
    const string host;
    Image image;
    array<Image*> users;
    signal<> imageChanged;
    /// Loads favicon for \a host
    Favicon(string&& host);
    /// Parses HTML link elements to find out favicon location
    void get(const URL& url, Map&& document);
    /// Updates shared users when receiving the real favicon
    void update();
};

/// Item with #link to an article
struct Entry : Item {
    /// Global unique identifier
    string guid;
    /// Link to the associated article
    string link;
    /// Favicon of the website
    Favicon* favicon=0;
    Entry(string&& guid, string&& link, Image&& icon, string&& text, int size=16)
        : Linear(Left),Item(move(icon),move(text),size), guid(move(guid)), link(move(link)){}
    Entry(string&& guid, string&& link, Favicon* favicon, string&& text, int size=16)
        : Linear(Left),Item(share(favicon->image),move(text),size), guid(move(guid)), link(move(link)), favicon(favicon){
        favicon->users << &icon.image;
    }
    ~Entry() { if(favicon) favicon->users.removeAll(&icon.image); }
};

/// List of entries fetched from feeds
/// \note .config/feeds contains the list of feeds to fetch, .config/read contains the list of read articles
struct Feeds : VBox, HighlightSelection {
    File readConfig;
    Map readMap;
    signal<> listChanged;
    signal< const ref<byte>& /*link*/, const ref<byte>& /*title*/, const Image& /*favicon*/ > pageChanged;
    array<unique<Favicon>> favicons; //store strong references to favicons (weakly referenced by entries)
    array<unique<Entry>> entries; // back referenced by favicons for asynchronous load

    Feeds();
    /// Polls all feeds
    void load();
    /// Returns whether the entry with \a title and \a link is read (according to config/read)
    bool isRead(const ref<byte>& title, const ref<byte>& link);
    /// Returns whether \a entry is read (according to config/read)
    bool isRead(const Entry& entry);
    /// Loads one feed
    void loadFeed(const URL&, Map&& document);
    /// If unread, appends entry at \a index to ~/.config/read
    void setRead(uint index);
    /// Sends pageChanged signal and preload next page
    void readEntry(uint index);
    /// Sets active entry as read, activate next unread entry
    void readNext();
};
