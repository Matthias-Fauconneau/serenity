#pragma once
#include "stream.h"
#include "process.h"
#include "signal.h"

/// \a Socket is a network socket
struct Socket : Buffer {
    int fd=0;
    Socket(){}
    Socket(Socket&& o){fd=o.fd; o.fd=0;}
    /// Connects to \a service on \a host
    bool connect(const string& host, const string& service);
    ~Socket();
    /// Socket
    virtual array<byte> read(int size)=0;
    virtual void write(const array<byte>& buffer)=0;
    /// Stream
    uint available(uint need) override;
    array<byte> get(uint size) override;
};

typedef struct ssl_st SSL;
struct SSLSocket : Socket {
    default_constructors(SSLSocket)
    SSL* ssl=0;
    bool connect(const string& host, const string& service, bool secure=false);
    ~SSLSocket();
    array<byte> read(int size) override;
    void write(const array<byte>& buffer) override;
};

struct TextSocket : TextStream, SSLSocket {
    default_constructors(TextSocket)
};

/// Encodes \a input to Base64 to transfer binary data through text protocol
string base64(const string& input);

struct URL {
    string scheme,authorization,host,path,fragment;
    URL(){}
    URL(const string& url);
    URL relative(URL&& url) const;
    explicit operator bool() { return host.size(); }
};
string str(const URL& url);
inline bool operator ==(const URL& a, const URL& b) {
    return a.scheme==b.scheme&&a.authorization==b.authorization&&a.host==b.host&&a.path==b.path&&a.fragment==b.fragment;
}

typedef delegate<void(const URL&, array<byte>&&)> Handler;

struct HTTP : Poll {
    TextSocket http;
    URL url;
    Handler handler;

    array<string> headers;
    string method;
    string content;
    array<string> redirect;

/// Connects to \a host and requests \a path using \a method.
/// \note \a headers and \a content will be added to request
/// \note If \a secure is true, an SSL connection will be used
/// \note HTTP should always be allocated on heap and no references should be taken.
    HTTP(const URL& url, Handler handler, array<string>&& headers={}, string&& method="GET"_, string&& content=""_, array<string>&& redirect={});

    void request();
    void event(pollfd) override;

    explicit operator bool() { return http.fd; }
};

/// Requests ressource at \a url and call \a handler when available
/// \note Persistent disk caching will be used, no request will be sent if cache is younger than \a maximumAge
void getURL(const URL &url, Handler handler, int maximumAge);

/// Returns path to cache file for \a url
string cacheFile(const URL& url);

/// descriptor to cache folder
extern int cache;
