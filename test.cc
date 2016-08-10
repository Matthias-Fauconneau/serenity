#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <wayland-client.h> // wayland-client
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

static void handle_ping(void*, struct wl_shell_surface *shell_surface, uint32 serial) { wl_shell_surface_pong(shell_surface, serial); }
static void handle_configure(void*, struct wl_shell_surface*, uint32 unused edges, int32_t unused width, int32_t unused height) {}
static void handle_popup_done(void*, struct wl_shell_surface*) {}
static const struct wl_shell_surface_listener shell_surface_listener = {handle_ping, handle_configure, handle_popup_done};

static void shm_format(void*, struct wl_shm*, uint32) {}
struct wl_shm_listener shm_listener = { shm_format };

static void global_registry_handler(void*, struct wl_registry *registry, uint32 id, const char *interface, uint32) {
 log(interface);
 if(str(interface)=="wl_compositor"_) compositor = (wl_compositor*)wl_registry_bind(registry, id, &wl_compositor_interface, 1);
 if(str(interface)=="wl_shell"_) shell = (wl_shell*)wl_registry_bind(registry, id, &wl_shell_interface, 1);
 if(str(interface)=="wl_shm"_) {
  shm = (wl_shm*)wl_registry_bind(registry, id, &wl_shm_interface, 1);
  wl_shm_add_listener(shm, &shm_listener, 0);
 }
}
static void global_registry_remover(void*, struct wl_registry*, uint32) {}
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

  while (wl_display_dispatch(display) != -1) {}

  wl_display_disconnect(display);
 }
} app;
