#pragma once
typedef unsigned int uint;

extern "C" {
#define WINAPI __stdcall

typedef void* HANDLE;
typedef HANDLE HINSTANCE;

struct FileAttribute {
	uint fileAttributes;
	long creationTime;
	long lastAccessTime;
	long lastWriteTime;
	uint fileSizeHigh;
	uint fileSizeLow;
};

enum { GENERIC_WRITE=0x40000000, GENERIC_READ=0x80000000 };
enum { FILE_SHARE_READ=1, FILE_SHARE_WRITE, OPEN_EXISTING };
enum { STD_ERROR_HANDLE=-12, STD_OUTPUT_HANDLE=-11, STD_INPUT_HANDLE=-10 };

HANDLE WINAPI CreateFileA(const char* path, int access, int shareMode, int securityAttributes, int creationDisposition, int flags, int templateFile);
bool WINAPI WriteFile(HANDLE hFile, const char* buffer, int size, int* written, int* overlapped);
bool WINAPI ReadFile(HANDLE hFile, char* buffer, int size, int* read, int* overlapped);
bool WINAPI GetFileAttributesExA(char* name, int type, FileAttribute* info);
HANDLE WINAPI GetStdHandle(int nStdHandle);
void WINAPI ExitProcess(uint exitCode) __attribute((noreturn));
}
