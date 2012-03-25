#pragma once
#include "stream.h"

/// \a Socket is a network socket
struct Socket : Buffer {
    int fd=0;
    /// Connects to \a service on \a host
    bool connect(const string& host, const string& service);
    ~Socket();
    virtual array<byte> read(int size)=0;
    virtual void write(const array<byte>& buffer)=0;
    uint available(uint need) override;
};

typedef struct ssl_st SSL;
struct SSLSocket : Socket {
    SSL* ssl=0;
    bool connect(const string& host, const string& service, bool secure=false);
    ~SSLSocket();
    array<byte> read(int size) override;
    void write(const array<byte>& buffer) override;
};

struct TextSocket : TextStream, SSLSocket {};

/// Encodes \a input to Base64 to transfer binary data through text protocol
string base64(const string& input);

struct URL {
    string scheme,authorization,host,path,fragment;
    URL(const string& url);
    URL relative(URL&& url) const;
};
string str(const URL& url);

/// Connects to \a host and requests \a path using \a method.
/// \note \a headers and \a content will be added to request
/// \note If \a secure is true, an SSL connection will be used
array<byte> http(const string& host, const string& path, bool secure=false,
                 const array<string>& headers={}, const string& method="GET"_, const string &content=""_);

/// Gets ressource at URL
/// \note Persistent disk caching will be used
array<byte> getURL(const URL &url);
