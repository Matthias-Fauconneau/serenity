
#if 0
#include "core.h"
//windows.h
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

}
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "core.h"
#if 1
extern "C" int WinMain(HINSTANCE,HINSTANCE,char*,int)  {
    HANDLE stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    WriteFile(stdout, "Hello World!\n", sizeof("Hello World!\n")-1, 0, 0);
    return 0;
}
#endif

#if 0
#include <stdio.h>
int main() {
    printf("Hello World!\n");
}
#endif

#if 0
#include <windows.h>
int WinMain(HINSTANCE,HINSTANCE,LPSTR,int)  {
  MessageBox(0,"Hello, Windows","MinGW Test Program",MB_OK);
  return 0;
}
#endif

#if 0
#include <windows.h>

char *AppTitle="Win1";
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

int WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int nCmdShow)
{
  WNDCLASS wc;
  HWND hwnd;
  MSG msg;

  wc.style=CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc=WindowProc;
  wc.cbClsExtra=0;
  wc.cbWndExtra=0;
  wc.hInstance=hInst;
  wc.hIcon=LoadIcon(NULL,IDI_WINLOGO);
  wc.hCursor=LoadCursor(NULL,IDC_ARROW);
  wc.hbrBackground=(HBRUSH)COLOR_WINDOWFRAME;
  wc.lpszMenuName=NULL;
  wc.lpszClassName=AppTitle;

  if (!RegisterClass(&wc))
    return 0;

  hwnd = CreateWindow(AppTitle,AppTitle,
    WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,CW_USEDEFAULT,100,100,
    NULL,NULL,hInst,NULL);

  if (!hwnd)
    return 0;

  ShowWindow(hwnd,nCmdShow);
  UpdateWindow(hwnd);

  while (GetMessage(&msg,NULL,0,0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
  switch (msg)
  {
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc;
      RECT r;
      GetClientRect(hwnd,&r);
      dc=BeginPaint(hwnd,&ps);
      DrawText(dc,"Hello World",-1,&r,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
      EndPaint(hwnd,&ps);
      break;
    }

    case WM_DESTROY:
      PostQuitMessage(0);
      break;

    default:
      return DefWindowProc(hwnd, msg, wparam, lparam);
  }
  return 0;
}
#endif
