#pragma once
/// \file http.h DNS queries, asynchronous HTTP requests, persistent disk cache
#include "data.h"
#include "process.h"
#include "function.h"
#include "file.h"

/// TCP network socket (POSIX)
struct TCPSocket : Socket {
    /// Connects to \a port on \a host
    TCPSocket(uint host, uint16 port);
};

/// SSL network socket (openssl)
struct SSLSocket : TCPSocket {
    SSLSocket(uint host, uint16 port, bool secure=false);
    move_operator(SSLSocket):TCPSocket(move(o)),ssl(o.ssl){o.ssl=0;}
    ~SSLSocket();
    struct SSL* ssl=0;
    array<byte> readUpTo(int size);
    void write(const ref<byte>& buffer);
};

/// Implements Data::available using Stream::readUpTo
template<class T/*: Stream*/> struct DataStream : T, virtual Data {
    template<Type... Args> DataStream(Args... args):T(args...){} //workaround lack of constructor inheritance support
    /// Feeds Data buffer using T::readUpTo
    uint available(uint need) override;
};

struct URL {
    string scheme,authorization,host,path,fragment;
    URL(){}
    /// Parses an absolute URL
    URL(const ref<byte>& url);
    /// Parses \a url relative to this URL
    URL relative(URL&& url) const;
    explicit operator bool() { return (bool)host; }
};
string str(const URL& url);
inline bool operator ==(const URL& a, const URL& b) {
    return a.scheme==b.scheme&&a.authorization==b.authorization&&a.host==b.host&&a.path==b.path&&a.fragment==b.fragment;
}

typedef function<void(const URL&, Map&&)> Handler;
/// Asynchronously fetches a file over HTTP
struct HTTP : DataStream<SSLSocket>, Poll, TextData {
    URL url;
    array<string> headers; ref<byte> method; //Request
    uint contentLength=0; bool chunked=false; array<string> redirect; //Header
    int chunkSize=0; array<byte> content; //Data
    Handler handler;

/// Connects to \a host and requests \a path using \a method.
/// \note \a headers and \a content will be added to request
/// \note If \a secure is true, an SSL connection will be used
/// \note HTTP should always be allocated on heap and no references should be taken.
    HTTP(URL&& url, Handler handler, array<string>&& headers =__(), const ref<byte>& method="GET"_);

   enum { Request, Header, Content, Cache, Handle, Done } state = Request;
    void request();
    void header();
    void event() override;
};

/// Requests ressource at \a url and call \a handler when available
/// \note Persistent disk caching will be used, no request will be sent if cache is younger than \a maximumAge minutes
void getURL(URL&& url, Handler handler=[](const URL&, Map&&){}, int maximumAge=24*60);

/// Returns path to cache file for \a url
string cacheFile(const URL& url);
