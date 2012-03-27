#pragma once
#include "stream.h"

/// \a Socket is a network socket
struct Socket : Buffer {
    int fd=0;
    Socket(){}
    Socket(Socket&& o){fd=o.fd; o.fd=0;}
    /// Connects to \a service on \a host
    bool connect(const string& host, const string& service);
    ~Socket();
    virtual array<byte> read(int size)=0;
    virtual void write(const array<byte>& buffer)=0;
    uint available(uint need) override;
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
    URL(const string& url);
    URL relative(URL&& url) const;
};
string str(const URL& url);

struct HTTP {
    TextSocket http;
    string host;
    string path;
    bool secure;
    array<string> headers;
    string method;
    string content;

    HTTP(HTTP&&)=default;

/// Connects to \a host and requests \a path using \a method.
/// \note \a headers and \a content will be added to request
/// \note If \a secure is true, an SSL connection will be used
    HTTP(const string& host, const string& path, bool secure=false,
         const array<string>& headers={}, const string& method="GET"_, const string& content=""_);
/// Reads answer from server
    array<byte> read();

    explicit operator bool() { return http.fd; }
};

/// Gets ressource at \a url
/// \note Persistent disk caching will be used
/// \note If \a wait is false \a url is not cached, request the ressource without blocking
array<byte> getURL(const URL &url, bool wait=true);
