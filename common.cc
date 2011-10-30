#include "common.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <dirent.h>

/// debug
void log_(void* n) { log_(toString((long)n,16,8)); }
void log_(long n) { log_(toString(n)); }
void log_(ulong n) { log_((long)n); }
void log_(int32 n) { log_((long)n); }
void log_(uint32 n) { log_((long)n); }
void log_(int16 n) { log_((long)n); }
void log_(uint16 n) { log_((long)n); }
void log_(int8 n) { log_((long)n); }
void log_(uint8 n) { log_((long)n); }
void log_(double n) { log_(toString(n)); }
void log_(float n) { log_((double)n); }

/// string

string toString(long number, int base, int pad) {
	assert(base>=2 && base<=16,"Unsupported base",base);
	char buf[20]; clear(buf); int i=19;
	long n = abs(number);
	if(n >= 0x10000) { base=16; pad=8; }
    do {
        buf[--i] = "0123456789ABCDEF"[n%base];
        n /= base;
    } while( n!=0 );
	while(19-i<pad) buf[--i] = '0';
    if(number<0) buf[--i] = '-';
    if(base==16) { buf[--i] = 'x'; buf[--i] = '0'; }
	string s(buf+i,19-i); s.detach(); return s;
}
string toString(double number) {
    return _(".")+toString(long(100*number));
}
long readInteger(const char*& s, int base) {
	assert(base>=2 && base<=16,"Unsupported base",base);
    int neg=0;
    if(*s == '-' ) s++, neg=1; else if(*s == '+') s++;
    long number=0;
    for(;*s;s++) {
        int n = string("0123456789ABCDEF",base).indexOf(*s);
        if(n < 0) break; //assert(n>=0,"Invalid integer",str);
        number *= base;
        number += n;
    }
    return neg ? -number : number;
}
long toInteger(const string& str, int base) { const char* s=str.data; return readInteger(s,base); }
double readFloat(const char*& s, int base ) {
    assert(base>2 && base<16,"Unsupported base",base);
    int neg=0;
    if(*s == '-') s++, neg=1; else if(*s == '+') s++;
    int exponent = 1;
    int significand = 0;
    for(bool gotDot=false;*s;s++) {
        if(*s == '.') { gotDot=true; continue; }
		int n = string("0123456789ABCDEF",base).indexOf(*s);
        if(n < 0) break; //assert(n>=0,"Invalid float",str);
        significand *= base;
        significand += n;
        if(gotDot) exponent *= base;
    }
	return neg ? -float(significand)/float(exponent) : float(significand)/float(exponent);
}
double readFloat(const string& str, int base ) { const char* s=str.data; return readFloat(s,base); }

string section(const string& s, char sep, int start, int end) {
    int b,e;
    if(start>=0) {
        b=0;
		for(int i=0;i<start && b<s.size;b++) if(s[b]==sep) i++;
    } else {
		b=s.size;
		if(start!=-1) for(int i=0;b-->0;) { if(s[b]==sep) { i++; if(i>=-start-1) break; } }
        b++; //skip separator
    }
    if(end>=0) {
        e=0;
        for(int i=0;e<s.size;e++) if(s[e]==sep) { i++; if(i>=end) break; }
    } else {
		e=s.size;
		if(end!=-1) for(int i=0;e-->0;) { if(s[e]==sep) { i++; if(i>=-end-1) break; } }
    }
	return s.slice(b,e-b);
}
string strz(const string& s) { return s+_("\0"); }

/// stream

long TextStream::readInteger(int base) { auto b=(const char*)&data[i]; auto e=b; long r = ::readInteger(e,base); i+=int(e-b); return r; }
double TextStream::readFloat(int base) { auto b=(const char*)&data[i]; auto e=b; auto r = ::readFloat(e,base); i+=int(e-b); return r; }

/// file

bool exists(const string& path) {
    int fd = open(strz(path).data, O_RDONLY);
    if(fd >= 0) { close(fd); return true; }
    return false;
}

struct stat statFile(const string& path) { struct stat file; stat(strz(path).data, &file); return file; }
bool isDirectory(const string& path) { return statFile(path).st_mode&S_IFDIR; }

array<string> listFiles(const string& folder, bool recursive) {
	array<string> list;
	DIR* dir = opendir(strz(folder).data);
	assert(dir);
	for(dirent* dirent; (dirent=readdir(dir));) {
		string name = string(dirent->d_name,(int)strlen(dirent->d_name));
		if(name!=_(".") && name!=_("..")) {
			string path = folder+_("/")+name;
			if(recursive && isDirectory(path)) list << move(listFiles(path)); else list << move(path);
		}
	}
	closedir(dir);
	return list;
}

int createFile(const string& path) {
    return open(strz(path).data,O_CREAT|O_WRONLY|O_TRUNC,0666);
}

string mapFile(const string& path) {
    int fd = open(strz(path).data, O_RDONLY);
    assert(fd >= 0,"File not found",path);
    struct stat sb; fstat(fd, &sb);
	const void* data = mmap(0,(size_t)sb.st_size,PROT_READ,MAP_PRIVATE,fd,0); //|MAP_HUGETLB/*avoid seeks*/
    close(fd);
	return string((const char*)data,(int)sb.st_size);
}

/// time

int time() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return int(ts.tv_sec*1000+ts.tv_nsec/1000000); }
