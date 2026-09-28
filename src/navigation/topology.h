#ifndef NAVIGATION_TOPOLOGY_H
#define NAVIGATION_TOPOLOGY_H

#include "../core_types.h"

// Navigation topology (topology.c): a navigation's questions about Desktops
// and displays, answered from one WindowServer reply.
//
// Thread: event loop.
// State: g_space_navigation_spaces, every Desktop's order, display and type
// and each display's current Desktop. Command reads it for each request and
// each queued step it runs, and unloads it afterwards; unloaded, every query
// asks WindowServer.
// Callers: command (reading, order and offsets), step (display, visibility,
// type and the other displays' Desktops and windows for the raise check).

#define SPACE_NAVIGATION_DISPLAYS_MAX 16
#define SPACE_NAVIGATION_WINDOWS_MAX 256

static void space_navigation_spaces_load(CFArrayRef displays);
static void space_navigation_spaces_read(void);
static bool space_navigation_spaces_loaded(void);
static void space_navigation_spaces_unload(void);
static int space_navigation_spaces_index(uint64_t sid);
static uint64_t space_navigation_spaces_at(int index);
static int space_navigation_step_index(int index, int count, int steps);
static uint64_t space_navigation_spaces_offset(uint64_t sid, int steps);
static uint32_t space_navigation_space_display(uint64_t sid);
static uint64_t space_navigation_display_space(uint32_t did);
static bool space_navigation_space_visible(uint64_t sid);
static bool space_navigation_space_fullscreen(uint64_t sid);
static int space_navigation_spaces_visible_elsewhere(uint32_t did, uint64_t *list);
static int space_navigation_spaces_windows(uint64_t *spaces, int space_count, int cid, uint32_t *ids, int capacity);

#endif
