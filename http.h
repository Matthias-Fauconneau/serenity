#pragma once
/// \file http.h DNS queries, asynchronous HTTP requests, persistent disk cache
#include "data.h"
#include "process.h"
#include "function.h"
#include "file.h"
#include "image.h"

/// TCP network socket (POSIX)
struct TCPSocket : Socket {
    /// Connects to \a port on \a host
    TCPSocket(uint host, uint16 port);
};

/// SSL network socket (openssl)
struct SSLSocket : TCPSocket {
    SSLSocket(uint host, uint16 port, bool secure=false);
    ~SSLSocket();

    array<byte> readUpTo(int size);
    void write(const ref<byte>& buffer);

    handle<struct SSL*> ssl;
};

/// Implements Data::available using Stream::readUpTo
template<class T/*: Stream*/> struct DataStream : T, virtual Data {
    using T::T;
    /// Feeds Data buffer using T::readUpTo
    uint available(uint need) override;
};

struct URL {
    URL(){}
    /// Parses an absolute URL
    URL(const ref<byte>& url);
    /// Parses \a url relative to this URL
    URL relative(URL&& url) const;
    explicit operator bool() { return (bool)host; }

    string scheme, authorization, host, path, fragment;
};
string str(const URL& url);
inline bool operator ==(const URL& a, const URL& b) {
    return a.scheme==b.scheme&&a.authorization==b.authorization&&a.host==b.host&&a.path==b.path&&a.fragment==b.fragment;
}

/// Returns path to cache file for \a url
string cacheFile(const URL& url);

/// Requests ressource at \a url and call \a handler when available
/// \note Persistent disk caching will be used, no request will be sent if cache is younger than \a maximumAge minutes
void getURL(URL&& url, function<void(const URL&, Map&&)> handler=[](const URL&, Map&&){}, int maximumAge=24*60);

/// Requests image at \a url and call \a handler when available (if was not cached)
void getImage(URL&& url, Image* target, function<void()> imageLoaded, int2 size=0, uint maximumAge=24*60);
