// Virtual pointer for the e2e test compositors. Reads lines from stdin:
//   abs X Y   (logical pixels; drive.sh passes the window size as extents)
//   press   release   scroll DX DY   sleep MS
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-client.h>
#include "vp.h"

static struct zwlr_virtual_pointer_manager_v1 *mgr;
static struct wl_seat *seat;

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v)
{
    (void)d; (void)v;
    if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
        mgr = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name))
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
}
static void global_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener listener = { global, global_remove };

static uint32_t now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
    unsigned w = argc > 2 ? atoi(argv[1]) : 1600, h = argc > 2 ? atoi(argv[2]) : 1000;
    struct wl_display *dpy = wl_display_connect(NULL);
    if (!dpy) return 1;
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &listener, NULL);
    wl_display_roundtrip(dpy);
    if (!mgr) { fprintf(stderr, "no virtual pointer manager\n"); return 1; }
    struct zwlr_virtual_pointer_v1 *p = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, seat);
    wl_display_roundtrip(dpy);
    char line[128];
    while (fgets(line, sizeof line, stdin)) {
        unsigned x, y, ms;
        int dx, dy;
        if (sscanf(line, "abs %u %u", &x, &y) == 2)
            zwlr_virtual_pointer_v1_motion_absolute(p, now(), x, y, w, h);
        else if (!strncmp(line, "press", 5))
            zwlr_virtual_pointer_v1_button(p, now(), BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
        else if (!strncmp(line, "release", 7))
            zwlr_virtual_pointer_v1_button(p, now(), BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        else if (sscanf(line, "scroll %d %d", &dx, &dy) == 2) {
            zwlr_virtual_pointer_v1_axis_source(p, WL_POINTER_AXIS_SOURCE_WHEEL);
            if (dx) {
                zwlr_virtual_pointer_v1_axis_discrete(p, now(),
                    WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_int(dx * 10), dx);
            }
            if (dy) {
                zwlr_virtual_pointer_v1_axis_discrete(p, now(),
                    WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(dy * 10), dy);
            }
        }
        else if (sscanf(line, "sleep %u", &ms) == 1) { wl_display_flush(dpy); usleep(ms * 1000); continue; }
        else continue;
        zwlr_virtual_pointer_v1_frame(p);
        wl_display_flush(dpy);
    }
    wl_display_roundtrip(dpy);
    return 0;
}
