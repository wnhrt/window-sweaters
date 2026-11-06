#pragma once
// Knitted window borders, rendered with Cairo.
//
// A port of the macOS renderer (src/knit.c) to Cairo, so Hyprland windows can
// wear the same sweaters. The geometry, the yarn shading and the colourways are
// the same; only the drawing backend differs.
//
// The band is the ring between the window's rounded rect and that rect grown
// by `band`. The stitch pattern is strictly periodic, so it is rendered once
// into a small seamless tile and then tiled into the ring, one mitred piece of
// knitting per side.
//
// Coordinates are y-down (Cairo's native orientation), in logical pixels.
// Apply the output scale to the context before calling knit_draw().

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum knit_stitch { KNIT_STOCKINETTE = 0, KNIT_RIB, KNIT_GARTER, KNIT_STITCH_COUNT };

struct knit_gauge {
  float rows;        // chart rows across the band (plain knitting: stitch rows)
  float aspect;      // stitch width / row height; real stockinette is ~1.35
  float row_overlap; // how far each row nests into the one above (0..1)
  float yarn;        // yarn thickness, as a fraction of stitch width
  float bow;         // how much the arms of the stitch bow outward (0..1)
  float jitter;      // hand-knit wobble, as a fraction of stitch width
  float ground;      // brightness of the wool behind the stitches
  float shadow;      // brightness of the shadow cast under each stitch
  float light;       // brightness of the yarn itself
  float depth;       // how far the shadow is offset, in yarn widths
  float ambient;     // light present everywhere; raise it to keep hues vivid
  float relief;      // how steeply the yarn stands up under the light
  float sheen;       // strength of the directional light on the yarn
  float tuck;        // how far the knit continues UNDER the window edge
};

struct knit_basket { const char* name; const uint32_t* colors; int len; };

extern struct knit_gauge g_knit;
extern int g_knit_stitch;       // enum knit_stitch
extern int g_knit_basket;       // index into g_knit_baskets

// Where each unpatched side's pattern repeat is anchored.
//   KNIT_ANCHOR_CORNER  cast on at the mitre corner; steady while resizing.
//   KNIT_ANCHOR_CENTRE  the repeat is centred on each side.
enum knit_anchor { KNIT_ANCHOR_CORNER = 0, KNIT_ANCHOR_CENTRE };
extern int g_knit_anchor;
extern const struct knit_basket g_knit_baskets[];
extern const int g_knit_basket_count;
extern const char* g_knit_stitch_names[];

uint32_t knit_color_for_window(uint32_t id);
// Stable fallback for an app without a curated colourway.
uint32_t knit_color_for_app(const char* app);

/// The shared Zigzag's contrast yarn for a main yarn: the collection's cream,
/// or, on a pale yarn (HSL lightness >= 0.62), a deeper shade of that yarn.
#define KNIT_CREAM 0xfff6f0deu
uint32_t knit_zigzag_contrast(uint32_t base);

/// murmur3 finalizer, shared by the noise and the colour hashing.
uint32_t knit_mix(uint32_t h);

/// Draw a knitted band in the ring between `x,y,w,h` (the window rect, in the
/// context's user space) and that rect grown by `band`.
///
/// radius  the window's real corner radius
/// color   base yarn colour (ARGB)
/// chart   index into g_charts, or -1 for plain knitting
/// dim     darkening of unfocused windows (the yarn remains opaque)
/// tuck    how far the knit continues under the window edge (>= 1)
void knit_draw(cairo_t* cr, double x, double y, double w, double h, float radius,
               float band, uint32_t color, int chart, float dim, float tuck);

/// Discard every cached tile. Call after changing the gauge, stitch or charts.
void knit_flush_cache(void);

/// Bumped by knit_flush_cache(). Anything caching rendered bands re-checks it.
extern unsigned g_knit_revision;

#ifdef __cplusplus
}
#endif
