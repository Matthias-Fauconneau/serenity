#pragma once
#include "stream.h"

/// \a Socket is a network socket
struct Socket : Buffer {
    int fd=0;
    /// Connects to \a service on \a host
    void connect(const string& host, const string& service);
    ~Socket();
    virtual array<byte> read(int size)=0;
    virtual void write(const array<byte>& buffer)=0;
    uint available(uint need) override;
};

typedef struct ssl_st SSL;
struct SSLSocket : Socket {
    SSL* ssl=0;
    void connect(const string& host, const string& service, bool secure=false);
    ~SSLSocket();
    array<byte> read(int size) override;
    void write(const array<byte>& buffer) override;
};

struct TextSocket : TextStream, SSLSocket {};

/// Encodes \a input to Base64 to transfer binary data through text protocol
string base64(const string& input);

struct HTTP {
    TextSocket http;
    string host;
    string authorization;
    bool chunked=false;

    /// Create an HTTP connection to \a host
    HTTP(string&& host, bool secure=false, string&& authorization=""_);
    array<byte> request(const string& path, const string& method, const string& post, const string &header=""_);
    array<byte> get(const string& path, const string &header=""_);
    static array<byte> getURL(const string& url);
    array<byte> post(const string& path, const string& content);
};
