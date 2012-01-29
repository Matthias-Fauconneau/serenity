#pragma once
#include "array.h"

/// utf8_iterator is used to iterate UTF-8 encoded strings
struct utf8_iterator {
    const char* pointer;
    utf8_iterator(const char* pointer):pointer(pointer){}
    bool operator!=(const utf8_iterator& o) const { return o.pointer != pointer; }
    int operator* () const {
        int code = pointer[0];
        if(code&0b10000000) {
            bool test = code&0b00100000;
            code &= 0b00011111; code <<= 6; code |= pointer[1]&0b111111;
            if(test) {
                bool test = code&0b10000000000;
                code &= 0b00001111111111; code <<= 6; code |= pointer[2]&0b111111;
                if(test) {
                    bool test = code&0b1000000000000000;
                    if(test) fail();
                    code &= 0b00000111111111111111; code <<= 6; code |= pointer[3]&0b111111;
                }
            }
        }
        return code;
    }
    const utf8_iterator& operator++() {
        int code = *pointer++;
        if(code&0b10000000) { pointer++;
            if(code&0b00100000) { pointer++;
                if(code&0b00010000) { pointer++;
                    if(code&0b00001000) fail();
                }
            }
        }
        return *this;
    }
};

/// \a string is an \a array of characters with specialized methods for UTF-8 string handling
//TODO: proper multibyte encoding support
struct string : array<char> {
    //using array<char>::array<char>;
    string() {}
    explicit string(int capacity):array<char>(capacity){}
    string(array<char>&& o):array<char>(move(o)){}
    string(const char* data, int size):array<char>(data,size){}
    string(const char* begin,const char* end):array<char>(begin,end){}

    const utf8_iterator begin() const { return data; }
    const utf8_iterator end() const { return data+size; }

    bool startsWith(const string& a) const { return slice(0,a.size)==a; }
    bool endsWith(const string& a) const { return slice(size-a.size)==a; }

    string replace(const string& before, const string& after) const {
        string r(size);
        for(int i=0;i<size;) { //->utf8_iterator
            if(i<=size-before.size && slice(i,before.size)==before) { r<<after.copy(); i+=before.size; }
            else { r<<data[i]; i++; }
        }
        return r;
    }
};

/// Returns a null-terminated string
string strz(const string& s);
/// Returns a bounded reference to the null-terminated string pointer
string strz(const char* s);

/// Converts a machine integer to its human-readable representation
string toString(long n, int base=10, int pad=0);
//inline string toString(int n, int base=10, int pad=0) { return toString((long)n,base,pad); }
/// Converts a floating point number to its human-readable representation
//TODO: variable decimal precision
//string toString(double number);

/// Parses an integer value
long toInteger(const string& str, int base=10 );
/// Parses an integer value and set \a s after it
long readInteger(const char*& s, int base=10);
/// Parses a decimal value
double toFloat(const string& str, int base=10 );
/// Parses a decimal value and set \a s after it
double readFloat(const char*& s, int base=10 );
/// Returns a reference to the string between the "start"th and "end"th occurence separator \a sep
/// \note you can use negative \a start, \a end to count from the right
/// \note this is a non-copying alternative to join(split(str,sep).slice(start,end),sep)
string section(const string& str, char sep, int start=0, int end=1);
/// Splits \a str wherever \a sep occurs
array<string> split(const string& str, char sep=' ');

/// Lexically compare strings
inline bool operator <(const string& a, const string& b) {
	for(int i=0;i<min(a.size,b.size);i++) {
		if(a[i] > b[i]) return false;
		if(a[i] < b[i]) return true;
	}
	return a.size < b.size;
}

/// operator + can be used to concatenate arrays
template <class A, class T> struct cat {
	const A& a; const array<T>& b;
	struct { cat* c; operator int() const { return c->a.size+c->b.size; } } size;
	cat(const A& a,const array<T>& b) : a(a), b(b) { size.c=this; }
    void copy(T* data) const { a.copy(data); ::copy(data+a.size,&b,b.size); }
    operator array<T>() { array<T> r; r.reserve(size); copy((T*)&r); r.size=size; return r; }
    operator string() { return operator array<T>(); } //C++ resolve a single implicit conversion
};
template <class A, class T> cat<A,T> operator +(const A& a,const array<T>& b) { return cat<A,T>(a,b); }

/// Stream (TODO -> stream.h)

/// create an array<byte> reference of a types's raw memory representation
template<class T> array<byte> raw(const T& t) { return array<byte>((byte*)&t,sizeof(T)); }

constexpr uint32 swap32(uint32 x) { return ((x&0xff000000)>>24)|((x&0x00ff0000)>>8)|((x&0x0000ff00)<<8)|((x&0x000000ff)<<24); }
constexpr uint16 swap16(uint16 x) { return ((x>>8)&0xff)|((x&0xff)<<8); }

/// \a Stream provides a convenient interface for reading binary data or text
enum Endianness { LittleEndian,BigEndian };
template<Endianness endianness=LittleEndian> struct Stream {
    const byte* data;
    uint size;
    uint pos=0;
    Stream(const array<byte>& data) : data(&data), size(data.size) {}

    /// Returns true if there is data to read
    explicit operator bool() const { return pos<size; }
    /// Peeks at the next \a size \a T elements in stream without moving \a pos
    template<class T=char> array<T> peek(int size) const { return array<T>((T*)(data+pos),size); }
    /// If stream content match \a key, advances \a pos by \a key size
    template<class T> bool match(const array<T>& key) { if(peek(key.size) == key) { pos+=key.size; return true; } else return false; }

    /// Reads one raw \a T element from stream
    template<class T=char> T read() {
        assert(pos+sizeof(T)<=size); T t = *(T*)(data+pos); pos+=sizeof(T); return t;
    }
    /// Reads \a size raw \a T elements from stream
    template<class T=char> array<T> read(int size) {
        assert((pos+size*sizeof(T))<=this->size); T* t = (T*)(data+pos); pos+=size*sizeof(T); return array<T>(t,size);
    }
    /// Read an array of raw \a T elements from stream with the array size encoded as an uint32
    template<class T=char> array<T> readArray() { uint32 size=read(); return read<T>(size); }

    /// Read an array of raw \a T elements from stream until \a end is read
    template<class T=char, class E> array<T> readUntil(E end) {
        uint length=0; for(;length<size-pos;length++) if(*(E*)(data+pos+length*sizeof(T))==end) break;
        return read(length);
    }

    struct ReadOperator {
        Stream * s;
        /// Reads an uint32 and if necessary, swaps from stream to host byte order
        operator uint32() { return endianness==BigEndian?swap32(s->read<uint32>()):s->read<uint32>(); }
        /// Reads an uint16 and if necessary, swaps from stream to host byte order
        operator uint16() { return endianness==BigEndian?swap16(s->read<uint16>()):s->read<uint16>(); }
        /// Reads a \a T element (deduced from the destination (return type overload))
        template<class T> operator T() { return s->read<T>(); }
	};
    /// Returns an operator to deduce the type to read from the destination (return type overload)
    ReadOperator read() { return ReadOperator{this}; }

    /// Skips a byte from the stream
    Stream& operator ++(int) { pos++; return *this; }
    /// Skips \a count byte from the stream
    Stream& operator +=(int count) { pos+=count; return *this; }
    /// Align the stream position to the next \a width
    template <int width> void align() { pos=::align<width>(pos); }
    //uint8 operator*() { return data[pos]; }
    //uint8 operator[](int offset) { return data[pos+offset]; }
    array<byte> slice(int offset, int size) const { return array<byte>(data+pos+offset,size); }
    operator array<byte>() { return slice(0,size-pos); }
};

/*struct TextStream : Stream<> {
	//TextStream(const char* data, int size) : Stream((uint8*)data,size) {}
	TextStream(const string& data) : Stream(data) {}
	long readInteger(int base=10);
	double readFloat(int base=10);
    //operator string() { return string((const char*)data+pos,size-pos); }
    //operator const char*() { return (const char*)data+pos; }
};*/
