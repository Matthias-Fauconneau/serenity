#pragma once
/// \file http.h DNS queries, asynchronous HTTP requests, persistent disk cache
#include "data.h"
#include "thread.h"
#include "function.h"
#include "file.h"
#include "image.h"

uint resolve(string host);

/// TCP network socket (POSIX)
struct TCPSocket : Socket {
 TCPSocket() {}
 /// Connects to \a port on \a host
 TCPSocket(uint host, uint16 port);
 void connect(uint host, uint16 port);
};

/// SSL network socket (openssl)
struct SSLSocket : TCPSocket {
 SSLSocket() {}
 SSLSocket(uint host, uint16 port, bool secure=false);
 ~SSLSocket();

 void connect(uint host, uint16 port, bool secure=false);
 int64 readUpTo(mref<byte> target);
 buffer<byte> readUpTo(int64 size);
 void write(const ref<byte>& buffer);
 void disconnect();

 handle<struct ssl_st*> ssl;
};

/// Implements Data::available using Stream::readUpTo
template<class T/*: Stream*/> struct DataStream : T, virtual Data {
 using T::T;
 /// Feeds Data buffer using T::readUpTo
 size_t available(size_t need) override;
};

struct URL {
 URL(){}
 /// Parses an absolute URL
 URL(string url);
 /// Parses \a url relative to this URL
 URL relative(URL&& url) const;
 explicit operator bool() const { return (bool)host; }

 String scheme, authorization, host, path, fragment, post;
};
String str(const URL& url);
inline bool operator ==(const URL& a, const URL& b) {
 return a.scheme==b.scheme&&a.authorization==b.authorization&&a.host==b.host&&a.path==b.path&&a.fragment==b.fragment&&a.post==b.post;
}
template<> inline URL copy(const URL& o) {
 URL url;
 url.scheme=copy(o.scheme); url.authorization=copy(o.authorization); url.host=copy(o.host); url.path=copy(o.path); url.fragment=copy(o.fragment); url.post=copy(o.post);
 return url;
}

/// Returns path to cache file for \a url
String cacheFile(const URL& url);

/// Asynchronously fetches a file over HTTP
struct HTTP : DataStream<SSLSocket>, Poll, TextData {
 HTTP() : state(Invalid) {}
 /// Connects to \a host and requests \a path
 /// \note \a headers and \a content will be added to request
 /// \note If \a secure is true, an SSL connection will be used
 HTTP(URL&& url, function<void(const URL&, Map&&)> contentAvailable={}, array<String>&& headers={});

 void request();
 void header();
 virtual void receiveContent();
 virtual void cache();
 void event() override;
 void done();

 // Request
 URL url;
 array<String> headers;
 function<void()> chunkReceived;
 function<void(const URL&, Map&&)> contentAvailable;
 // Header
 size_t contentLength=0;
 bool chunked=false;
 array<String> redirect;
 int redirectCount = 0;
 // Data
 size_t chunkSize = 0; //invalid;
 File file;
 array<byte> content;
 enum State { Invalid, Request, Header, Redirect, Denied, BadRequest, Content, Cache, Available, Handled };
 State state = Request;

 operator bool() { return state != Invalid; }
};

/// Requests ressource at \a url and call \a contentAvailable when available
/// \note Persistent disk caching will be used, no request will be sent if cache is younger than \a maximumAge hours
Map getURL(URL&& url, function<void(const URL&, Map&&)> contentAvailable={}, int maximumAge=24, HTTP::State wait=HTTP::Available);

/// Requests image at \a url and call \a contentAvailable when available (if was not cached)
void getImage(URL&& url, Image* target, function<void()> imageLoaded, int2 size=0, uint maximumAge=24*60);

