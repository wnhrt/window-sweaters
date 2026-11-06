#pragma once
// Icon-derived colourways: By App sweaters for apps without an authored rule,
// and the shared Zigzag's main yarn for every app (port of src/autoyarn.m).
//
// Everything here runs on the caller's (the compositor's) thread except the
// icon lookup and decoding, which run on one background worker that never
// touches renderer state. Results are handed over by knit_auto_poll().
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// A first request schedules icon work and returns false; output parameters
/// remain unchanged until a colourway is ready. Authored app rules always win.
/// `app` is the window's class (Wayland app id).
bool knit_auto_yarn(const char* app, uint32_t* yarn, int* chart);

/// Deliver finished icon work. Calls `ready(app, data)` for every app whose
/// colourway just became available, so its windows can be redrawn. Returns
/// true while work is still outstanding.
bool knit_auto_poll(void (*ready)(const char* app, void* data), void* data);

/// The shared Zigzag pattern: true while it is the selected pattern.
bool knit_zigzag_active(void);

/// Shared Zigzag for any app, the hand-designed ones included: its main yarn
/// from its icon (unchanged while sampling, or if the icon has no colour), and
/// a cream zigzag, or a deeper shade of that yarn when the yarn is pale.
/// *yarn comes in as the app's usual colour; *chart as the shared zigzag.
bool knit_zigzag_yarn(const char* app, uint32_t* yarn, int* chart);

/// Forget every sampled icon (for example after icon themes changed).
void knit_auto_reset(void);

/// Stop the worker. Safe to call more than once.
void knit_auto_shutdown(void);

// ---- icon sampling, usable on its own (tests, tools) ------------------------

/// Find the icon file for an app id via its .desktop entry and the icon
/// themes. Returns false if none was found.
bool knit_icon_path(const char* app, char* out, int capacity);

/// Base and contrast yarn from an icon file (PNG, or SVG when built with
/// librsvg). Returns false for an unreadable or colourless icon.
bool knit_icon_file_colours(const char* path, uint32_t* base, uint32_t* contrast);

/// The palette step alone, on 48x48 premultiplied ARGB pixels.
bool knit_icon_palette(const uint32_t* px, uint32_t* base, uint32_t* contrast);

#ifdef __cplusplus
}
#endif
