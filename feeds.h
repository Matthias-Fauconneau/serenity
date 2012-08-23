#pragma once
#include "interface.h"
#include "html.h"
#include "file.h"

/// Entry is an \a Item with a \a link to an article
struct Entry : Item {
    string link;
    Entry(string&& link, Image&& icon, string&& text, int size=16):Item(move(icon),move(text),size),link(move(link)){}
};

/// Feeds is a list of entries fetched from RSS/Atom feeds
/// \note .config/feeds contains the list of feeds to fetch, .config/read contains the list of read articles
struct Feeds : List<Entry> {
    int config;
    File readConfig;
    Map readMap;
    signal<> listChanged;
    signal< const ref<byte>& /*link*/, const ref<byte>& /*title*/, const Image& /*favicon*/ > pageChanged;
    map<string/*link*/, Image> favicons; //store strong references to favicons weakly referenced by entries

    /// Polls all feeds on startup
    Feeds();
    /// Returns whether the entry with \a title and \a link is read (according to config/read)
    bool isRead(const ref<byte>& title, const ref<byte>& link);
    /// Returns whether \a entry is read (according to config/read)
    bool isRead(const Entry& entry);
    /// Loads an RSS/Atom feed
    void loadFeed(const URL&, array<byte>&& document);
    /// Parses HTML link elements to find out favicon location
    void getFavicon(const URL& url, array<byte>&& document);
    /// If unread, appends entry at \a index to config\read
    void setRead(uint index);
    /// Sends pageChanged signal and preload next page
    void readEntry(uint index);
    /// Sets active entry as read, activate next unread entry
    void readNext();
};
