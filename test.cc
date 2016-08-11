#if 1 // KGLOBALACCEL_TEST
#include "dbus.h"

struct Test {
 Test() {
  DBus dbus;
  //org.kde.kglobalaccel /kglobalaccel setShortcut
 }
} test;
#endif
#if WAYLAND_TEST
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h> // wayland-client
//#include <linux/input.h>
#include "thread.h"
#include "image.h"

File anonymousFile(off_t size) {
 strz name = (string)(environmentVariable("XDG_RUNTIME_DIR")+"/wayland-shared-XXXXXX");
 File file(mkostemp((char*)(const char*)name, O_CLOEXEC));
 remove(name);
 file.resize(size);
 return file;
}

static wl_compositor* compositor = 0;
static wl_shell* shell = 0;
static wl_shm* shm = 0;
static wl_seat *seat = 0;
static wl_keyboard *keyboard = 0;

static void keymap(void*, wl_keyboard*, uint unused format, int unused fd, uint unused size) {}
static void enter(void*, wl_keyboard*, uint unused serial, wl_surface*, wl_array* unused keys) {}
static void leave(void*, wl_keyboard*, uint unused serial, wl_surface*) {}
static void key(void*, wl_keyboard*, uint unused serial, uint unused time, uint key, uint state) {
 log(key, state);
 // 1 Escape
 // 64 BrightnessDown
 // 65 BrightnessUp
 // 116 Power
}
static void modifiers(void*, wl_keyboard*, uint unused serial, uint mods_depressed, uint mods_latched, uint mods_locked, uint group) {
 log(mods_depressed, mods_latched, mods_locked, group);
}
static void repeat_info(void*, wl_keyboard*, int unused rate, int unused delay) {}
static const wl_keyboard_listener keyboard_listener = {keymap, enter, leave, key, modifiers, repeat_info};

static void capabilities(void*, wl_seat* seat, uint caps) {
 if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
  keyboard = wl_seat_get_keyboard(seat);
  wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
 }
}
static void name(void*, wl_seat*, const char* unused name) {}
static const wl_seat_listener seat_listener = { capabilities, name };

static void configure_callback(void*, wl_callback*, uint unused time) {}

static wl_callback_listener configure_callback_listener = {
 configure_callback,
};

static void handle_ping(void*, wl_shell_surface *shell_surface, uint serial) { wl_shell_surface_pong(shell_surface, serial); }
static void handle_configure(void*, wl_shell_surface*, uint unused edges, int32_t unused width, int32_t unused height) {}
static void handle_popup_done(void*, wl_shell_surface*) {}
static const wl_shell_surface_listener shell_surface_listener = {handle_ping, handle_configure, handle_popup_done};

static void shm_format(void*, wl_shm*, uint) {}
wl_shm_listener shm_listener = { shm_format };

static void global_registry_handler(void*, wl_registry *registry, uint id, const char *interface, uint) {
 log(interface);
 if(str(interface)=="wl_compositor"_) compositor = (wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
 if(str(interface)=="wl_shell"_) shell = (wl_shell*)wl_registry_bind(registry, id, &wl_shell_interface, 1);
 if(str(interface)=="wl_shm"_) {
  shm = (wl_shm*)wl_registry_bind(registry, id, &wl_shm_interface, 1);
  wl_shm_add_listener(shm, &shm_listener, 0);
 }
 if(str(interface)=="wl_seat"_) {
  seat = (wl_seat*)wl_registry_bind(registry, id, &wl_seat_interface, 1);
  wl_seat_add_listener(seat, &seat_listener, 0);
 }
}
static void global_registry_remover(void*, wl_registry*, uint) {}
static const wl_registry_listener registry_listener = {global_registry_handler, global_registry_remover };

struct Test {
 Test() {
  wl_display *display = wl_display_connect(0);
  assert_(display);
  wl_registry *registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, 0);
  wl_display_dispatch(display);
  wl_display_roundtrip(display);
  assert_(compositor);
  wl_surface* surface = wl_compositor_create_surface(compositor);
  assert_(surface);
  wl_shell_surface* shell_surface = wl_shell_get_shell_surface(shell, surface);
  assert_(shell_surface);
  wl_shell_surface_set_toplevel(shell_surface);
  wl_shell_surface_add_listener(shell_surface, &shell_surface_listener, 0);

  Image target(buffer<byte4>(), int2(256));
  int stride = target.size.x * 4; // 4 bytes per pixel
  int size = stride * target.size.y;
  assert_(size, target.size);
  File file = anonymousFile(size);
  Map map(file, Map::Prot(Map::Read|Map::Write));
  target.data = (byte4*)map.data;
  target.ref::size = map.size/sizeof(byte4);
  target.clear(0xFF);
  wl_shm_pool* pool = wl_shm_create_pool(shm, file.fd, size);
  wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, target.size.x, target.size.y, stride, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);

  wl_surface_attach(surface, buffer, 0, 0);
  wl_surface_damage(surface, 0, 0, target.size.x, target.size.y);
  wl_surface_commit(surface);

  wl_callback* callback = wl_display_sync(display);
  wl_callback_add_listener(callback, &configure_callback_listener, 0);

  while (wl_display_dispatch(display) != -1) {}

  wl_display_disconnect(display);
 }
} app;
#endif
