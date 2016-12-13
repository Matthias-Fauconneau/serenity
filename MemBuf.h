#pragma once
#include <streambuf>

using std::basic_streambuf;


class MemBuf : public basic_streambuf<char>
{
public:
    MemBuf(char *p, size_t n)
    {
        setg(p, p, p + n);
        setp(p, p + n);
    }
};
