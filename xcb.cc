//gcc xcb.cc -lxcb -o debug/xcb
#include <xcb/xcb.h>
int main() { xcb_connect (NULL, NULL); }
